/** Navigation, hover, symbols, signature help, semantic tokens and folding. */
import {
  CodeAction,
  CodeActionKind,
  Diagnostic,
  DiagnosticSeverity,
  DiagnosticTag,
  DocumentHighlight,
  DocumentHighlightKind,
  DocumentSymbol,
  FoldingRange,
  FoldingRangeKind,
  Hover,
  Location,
  MarkupKind,
  ParameterInformation,
  Range,
  SemanticTokens,
  SemanticTokensBuilder,
  SignatureHelp,
  SignatureInformation,
  SymbolInformation,
  SymbolKind,
  WorkspaceEdit,
  TextEdit,
} from 'vscode-languageserver/node';
import { Analysis, MissingImportData, NDiagnostic } from './analyzer';
import * as ast from './ast';
import { createImportEdit } from './imports';
import { BUILTIN_ANNOTATIONS, KEYWORD_MAP, PRIMITIVE_MAP } from './builtins';
import { Span } from './lexer';
import { CLASS_LIKE, NSymbol, describe, paramsToString, typeToString } from './symbols';
import { Workspace } from './workspace';

export const TOKEN_TYPES = ['namespace', 'type', 'class', 'enum', 'interface', 'parameter', 'variable', 'property', 'enumMember', 'function', 'method', 'decorator', 'keyword'];
export const TOKEN_MODIFIERS = ['declaration', 'definition', 'readonly', 'static', 'abstract', 'deprecated', 'defaultLibrary', 'modification'];

const TOKEN_TYPE_INDEX = new Map(TOKEN_TYPES.map((t, i) => [t, i]));
const TOKEN_MODIFIER_INDEX = new Map(TOKEN_MODIFIERS.map((m, i) => [m, i]));

export function toRange(analysis: Analysis, span: Span): Range {
  return { start: analysis.lineMap.positionOf(span.start), end: analysis.lineMap.positionOf(span.end) };
}

export function toDiagnostic(analysis: Analysis, d: NDiagnostic): Diagnostic {
  const severity =
    d.severity === 'error' ? DiagnosticSeverity.Error : d.severity === 'warning' ? DiagnosticSeverity.Warning : d.severity === 'info' ? DiagnosticSeverity.Information : DiagnosticSeverity.Hint;
  const tags = d.tags?.map(t => (t === 'unnecessary' ? DiagnosticTag.Unnecessary : DiagnosticTag.Deprecated));
  return { range: toRange(analysis, d.span), message: d.message, severity, source: 'novus', tags, code: d.code, data: d.data };
}

// ------------------------------------------------------------ code actions

/** Quick fixes for missing imports in `[startOffset, endOffset]` plus "Add all missing imports". */
export function codeActions(analysis: Analysis, startOffset: number, endOffset: number, only?: string[]): CodeAction[] {
  const wants = (kind: string) => !only || only.some(k => kind === k || kind.startsWith(k + '.'));
  const actions: CodeAction[] = [];
  const missing = analysis.diagnostics.filter(d => d.code === 'missing-import');
  if (!missing.length) return actions;

  if (wants(CodeActionKind.QuickFix)) {
    const seen = new Set<string>();
    for (const d of missing) {
      if (d.span.end < startOffset || d.span.start > endOffset) continue;
      const data = d.data as MissingImportData;
      for (const pkg of data.packages) {
        if (seen.has(pkg)) continue;
        seen.add(pkg);
        actions.push({
          title: `Import '${pkg}'`,
          kind: CodeActionKind.QuickFix,
          diagnostics: [toDiagnostic(analysis, d)],
          isPreferred: seen.size === 1,
          edit: { changes: { [analysis.uri]: [createImportEdit(analysis, [pkg])] } },
        });
      }
    }
  }

  const all = analysis.missingImports();
  if (all.length && (wants(CodeActionKind.SourceFixAll) || wants(CodeActionKind.QuickFix))) {
    if (all.length > 1 || !actions.length) {
      actions.push({
        title: all.length === 1 ? `Import '${all[0]}'` : `Add all missing imports (${all.join(', ')})`,
        kind: wants(CodeActionKind.SourceFixAll) && only ? CodeActionKind.SourceFixAll : CodeActionKind.QuickFix,
        diagnostics: missing.map(d => toDiagnostic(analysis, d)),
        edit: { changes: { [analysis.uri]: [createImportEdit(analysis, all)] } },
      });
    }
  }
  return actions;
}

export function symbolLocation(ws: Workspace, sym: NSymbol, selection = true): Location | undefined {
  if (sym.builtin || !sym.uri) return undefined;
  const analysis = ws.get(sym.uri);
  if (!analysis) return undefined;
  return { uri: sym.uri, range: toRange(analysis, selection ? sym.selectionSpan : sym.span) };
}

function wordAt(text: string, offset: number): { word: string; start: number; end: number } | undefined {
  const isWord = (c: string) => /[A-Za-z0-9_]/.test(c);
  let start = offset;
  let end = offset;
  while (start > 0 && isWord(text[start - 1])) start--;
  while (end < text.length && isWord(text[end])) end++;
  if (start === end) return undefined;
  return { word: text.slice(start, end), start, end };
}

// ------------------------------------------------------------------- hover

export function symbolMarkdown(analysis: Analysis | undefined, sym: NSymbol): string {
  let signature = describe(sym);
  if ((sym.kind === 'variable' || sym.kind === 'constant' || sym.kind === 'field') && analysis) {
    const type = analysis.typeOfSymbol(sym);
    if (type && !sym.type) signature = signature.replace(/unknown$/, typeToString(type));
  }
  const parts = ['```novus', signature, '```'];
  if (sym.deprecated !== undefined) parts.push(`⚠️ **Deprecated**: ${sym.deprecated}`);
  if (sym.doc) parts.push(sym.doc);
  if (sym.container && (sym.kind === 'method' || sym.kind === 'field' || sym.kind === 'enumMember' || sym.kind === 'constructor')) {
    parts.push(`*Member of \`${sym.container.name}\`*`);
  }
  return parts.join('\n\n');
}

export function hover(ws: Workspace, analysis: Analysis, offset: number): Hover | undefined {
  const ref = analysis.referenceAt(offset);
  if (ref?.symbol) {
    const owner = ws.get(ref.symbol.uri) ?? analysis;
    return { contents: { kind: MarkupKind.Markdown, value: symbolMarkdown(owner, ref.symbol) }, range: toRange(analysis, ref.span) };
  }
  const w = wordAt(analysis.text, offset);
  if (!w) return undefined;
  const range = toRange(analysis, w);
  const prevChar = analysis.text[w.start - 1];
  if (prevChar === '@') {
    const ann = BUILTIN_ANNOTATIONS.find(a => a.name === w.word);
    if (ann) {
      const args = ann.args.length ? '\n\n' + ann.args.map(a => `- \`${a.name}: ${a.type}\` – ${a.doc}`).join('\n') : '';
      return { contents: { kind: MarkupKind.Markdown, value: `\`\`\`novus\n@${ann.name}\n\`\`\`\n\n${ann.doc}${args}\n\n_Built-in annotation._` }, range };
    }
  }
  const prim = PRIMITIVE_MAP.get(w.word);
  if (prim && ref?.tokenType === 'type') {
    const note = prim.implemented ? '_Implemented by novusc._' : '_Concept syntax – not yet implemented by novusc._';
    return { contents: { kind: MarkupKind.Markdown, value: `\`\`\`novus\n${prim.generic ? prim.name + '<T>' : prim.name}\n\`\`\`\n\n${prim.doc}\n\n${note}` }, range };
  }
  const kw = KEYWORD_MAP.get(w.word);
  if (kw && !ref) {
    return { contents: { kind: MarkupKind.Markdown, value: `\`\`\`novus\n${kw.detail}\n\`\`\`\n\n${kw.doc}` }, range };
  }
  if (prim) {
    const note = prim.implemented ? '_Implemented by novusc._' : '_Concept syntax – not yet implemented by novusc._';
    return { contents: { kind: MarkupKind.Markdown, value: `\`\`\`novus\n${prim.name}\n\`\`\`\n\n${prim.doc}\n\n${note}` }, range };
  }
  return undefined;
}

// -------------------------------------------------------------- navigation

export function definition(ws: Workspace, analysis: Analysis, offset: number): Location[] {
  const ref = analysis.referenceAt(offset);
  if (!ref?.symbol) return [];
  const target = ref.symbol.accessorOf ?? ref.symbol;
  const loc = symbolLocation(ws, target);
  return loc ? [loc] : [];
}

export function references(ws: Workspace, analysis: Analysis, offset: number, includeDeclaration: boolean): Location[] {
  const ref = analysis.referenceAt(offset);
  if (!ref?.symbol) return [];
  const id = ref.symbol.id;
  const out: Location[] = [];
  for (const a of ws.all()) {
    for (const r of a.references) {
      if (!r.symbol || r.symbol.id !== id) continue;
      if (r.declaration && !includeDeclaration) continue;
      out.push({ uri: a.uri, range: toRange(a, r.span) });
    }
  }
  return out;
}

export function documentHighlights(analysis: Analysis, offset: number): DocumentHighlight[] {
  const ref = analysis.referenceAt(offset);
  if (!ref?.symbol) return [];
  const id = ref.symbol.id;
  return analysis.references
    .filter(r => r.symbol && r.symbol.id === id)
    .map(r => ({ range: toRange(analysis, r.span), kind: r.declaration ? DocumentHighlightKind.Write : DocumentHighlightKind.Read }));
}

export function prepareRename(analysis: Analysis, offset: number): { range: Range; placeholder: string } | undefined {
  const ref = analysis.referenceAt(offset);
  if (!ref?.symbol || ref.symbol.builtin) return undefined;
  if (ref.symbol.kind === 'package' || ref.symbol.kind === 'module' || ref.symbol.kind === 'constructor') return undefined;
  return { range: toRange(analysis, ref.span), placeholder: ref.symbol.name };
}

export function rename(ws: Workspace, analysis: Analysis, offset: number, newName: string): WorkspaceEdit | undefined {
  const ref = analysis.referenceAt(offset);
  if (!ref?.symbol || ref.symbol.builtin) return undefined;
  const id = ref.symbol.id;
  const changes: Record<string, TextEdit[]> = {};
  for (const a of ws.all()) {
    for (const r of a.references) {
      if (!r.symbol || r.symbol.id !== id) continue;
      (changes[a.uri] ??= []).push({ range: toRange(a, r.span), newText: newName });
    }
  }
  return { changes };
}

// ----------------------------------------------------------------- symbols

function lspSymbolKind(sym: NSymbol): SymbolKind {
  switch (sym.kind) {
    case 'package':
      return SymbolKind.Package;
    case 'module':
      return SymbolKind.Module;
    case 'class':
    case 'abstract':
      return SymbolKind.Class;
    case 'enum':
      return SymbolKind.Enum;
    case 'interface':
    case 'annotation':
      return SymbolKind.Interface;
    case 'method':
      return sym.container && CLASS_LIKE.has(sym.container.kind) ? SymbolKind.Method : SymbolKind.Function;
    case 'constructor':
      return SymbolKind.Constructor;
    case 'field':
      return SymbolKind.Field;
    case 'constant':
      return SymbolKind.Constant;
    case 'variable':
    case 'parameter':
      return SymbolKind.Variable;
    case 'enumMember':
      return SymbolKind.EnumMember;
    case 'type':
      return SymbolKind.TypeParameter;
  }
}

function symbolDetail(sym: NSymbol): string {
  switch (sym.kind) {
    case 'method':
    case 'constructor':
      return `(${paramsToString(sym.params)})${sym.returnType ? ': ' + typeToString(sym.returnType) : ''}`;
    case 'field':
    case 'variable':
    case 'constant':
    case 'parameter':
      return sym.type ? typeToString(sym.type) : '';
    case 'class':
    case 'abstract':
    case 'interface':
      return sym.bases.length ? `based ${sym.bases.map(typeToString).join(', ')}` : '';
    default:
      return '';
  }
}

export function documentSymbols(analysis: Analysis): DocumentSymbol[] {
  const toDoc = (sym: NSymbol): DocumentSymbol | undefined => {
    if (!sym.name || sym.synthesized) return undefined;
    const range = toRange(analysis, sym.span);
    const selectionRange = toRange(analysis, sym.selectionSpan.end > sym.selectionSpan.start ? sym.selectionSpan : sym.span);
    const children = sym.members.map(toDoc).filter((d): d is DocumentSymbol => !!d);
    return {
      name: sym.kind === 'constructor' ? 'construct' : sym.name,
      detail: symbolDetail(sym),
      kind: lspSymbolKind(sym),
      range,
      selectionRange,
      children: children.length ? children : undefined,
      tags: sym.deprecated !== undefined ? [1] : undefined,
    };
  };
  return analysis.symbols.map(toDoc).filter((d): d is DocumentSymbol => !!d);
}

export function workspaceSymbols(ws: Workspace, query: string): SymbolInformation[] {
  const q = query.toLowerCase();
  const out: SymbolInformation[] = [];
  for (const a of ws.all()) {
    const visit = (sym: NSymbol, container?: NSymbol): void => {
      if (sym.name && !sym.synthesized && sym.kind !== 'package' && sym.kind !== 'module' && (!q || sym.name.toLowerCase().includes(q))) {
        out.push({ name: sym.name, kind: lspSymbolKind(sym), location: { uri: a.uri, range: toRange(a, sym.selectionSpan) }, containerName: container?.name });
      }
      for (const m of sym.members) if (!m.accessorOf) visit(m, sym);
    };
    for (const s of a.symbols) visit(s);
  }
  return out.slice(0, 500);
}

// ---------------------------------------------------------- signature help

function findOpenParen(text: string, offset: number): { paren: number; activeParameter: number } | undefined {
  let depth = 0;
  let commas = 0;
  let i = offset - 1;
  while (i >= 0) {
    const c = text[i];
    if (c === '"') {
      // skip string backwards
      i--;
      while (i >= 0 && !(text[i] === '"' && text[i - 1] !== '\\')) i--;
      i--;
      continue;
    }
    if (c === ')' || c === ']' || c === '}') depth++;
    else if (c === '(' || c === '[' || c === '{') {
      if (depth === 0) {
        if (c === '(') return { paren: i, activeParameter: commas };
        return undefined;
      }
      depth--;
    } else if (c === ',' && depth === 0) commas++;
    else if (c === '\n' && depth === 0) {
      // allow multi-line calls: keep scanning
    }
    i--;
  }
  return undefined;
}

export function signatureHelp(ws: Workspace, analysis: Analysis, offset: number): SignatureHelp | undefined {
  const open = findOpenParen(analysis.text, offset);
  if (!open) return undefined;
  const scope = analysis.scopeAt(offset);

  let candidates: NSymbol[] = [];
  let call: ast.CallExpr | undefined;
  ast.walk(analysis.program, node => {
    if (node.kind === 'Call' && node.parenStart === open.paren) call = node;
    return node.start <= open.paren && open.paren <= node.end + 1;
  });

  if (call) {
    const callee = call.callee;
    if (callee.kind === 'Ident') {
      const syms = analysis.lookup(callee.name, scope);
      candidates = syms.filter(s => s.kind === 'method');
      if (!candidates.length) {
        const cls = syms.find(s => CLASS_LIKE.has(s.kind));
        if (cls) candidates = analysis.membersOf(cls).filter(m => m.kind === 'constructor');
      }
    } else if (callee.kind === 'Member') {
      const objType = analysis.typeOf(callee.object, scope);
      candidates = analysis.membersOfType(objType, scope).filter(m => m.name === callee.name && (m.kind === 'method' || m.kind === 'constructor'));
    }
  } else {
    // parse failed – fall back to the identifier before the '('
    const before = analysis.text.slice(0, open.paren);
    const m = /([A-Za-z_]\w*)\s*$/.exec(before);
    if (m) {
      const syms = analysis.lookup(m[1], scope);
      candidates = syms.filter(s => s.kind === 'method');
    }
  }
  if (!candidates.length) return undefined;

  const signatures: SignatureInformation[] = candidates.map(sym => {
    const params = sym.params ?? [];
    const label = describe(sym);
    const paramInfos: ParameterInformation[] = params.map(p => {
      const text = `${p.type ? typeToString(p.type) + ' ' : ''}${p.name}`;
      const at = label.indexOf(text);
      return { label: at >= 0 ? [at, at + text.length] : text };
    });
    return { label, documentation: sym.doc ? { kind: MarkupKind.Markdown, value: sym.doc } : undefined, parameters: paramInfos };
  });

  let activeSignature = candidates.findIndex(s => (s.params ?? []).length > open.activeParameter);
  if (activeSignature < 0) activeSignature = 0;
  return { signatures, activeSignature, activeParameter: open.activeParameter };
}

// ---------------------------------------------------------- semantic tokens

export function semanticTokens(analysis: Analysis): SemanticTokens {
  const builder = new SemanticTokensBuilder();
  for (const ref of analysis.references) {
    const typeIndex = TOKEN_TYPE_INDEX.get(ref.tokenType);
    if (typeIndex === undefined) continue;
    const start = analysis.lineMap.positionOf(ref.span.start);
    const end = analysis.lineMap.positionOf(ref.span.end);
    if (start.line !== end.line) continue;
    let modifiers = 0;
    for (const m of ref.modifiers) {
      const idx = TOKEN_MODIFIER_INDEX.get(m);
      if (idx !== undefined) modifiers |= 1 << idx;
    }
    builder.push(start.line, start.character, end.character - start.character, typeIndex, modifiers);
  }
  return builder.build();
}

// ----------------------------------------------------------------- folding

export function foldingRanges(analysis: Analysis): FoldingRange[] {
  const out: FoldingRange[] = [];
  const add = (span: Span, kind?: FoldingRangeKind): void => {
    const start = analysis.lineMap.lineOf(span.start);
    const end = analysis.lineMap.lineOf(Math.max(span.start, span.end - 1));
    if (end > start) out.push({ startLine: start, endLine: end, kind });
  };
  ast.walk(analysis.program, node => {
    switch (node.kind) {
      case 'Define':
        if (node.bodySpan) add(node.bodySpan);
        break;
      case 'Method':
        if (node.body) add(node.body);
        break;
      case 'Block':
        add(node);
        break;
      case 'StructInit':
      case 'Map':
      case 'Array':
        add(node);
        break;
      case 'Annotation':
        if (node.args.length) add(node);
        break;
      default:
        break;
    }
    return true;
  });
  for (const c of analysis.comments) if (c.block) add(c, FoldingRangeKind.Comment);
  // consecutive line comments
  let runStart: number | undefined;
  let runEnd: number | undefined;
  let lastLine = -2;
  for (const c of analysis.comments) {
    if (c.block) continue;
    const line = analysis.lineMap.lineOf(c.start);
    if (line === lastLine + 1 && runStart !== undefined) {
      runEnd = line;
    } else {
      if (runStart !== undefined && runEnd !== undefined && runEnd > runStart) out.push({ startLine: runStart, endLine: runEnd, kind: FoldingRangeKind.Comment });
      runStart = line;
      runEnd = line;
    }
    lastLine = line;
  }
  if (runStart !== undefined && runEnd !== undefined && runEnd > runStart) out.push({ startLine: runStart, endLine: runEnd, kind: FoldingRangeKind.Comment });
  // imports
  const imports = analysis.program.items.filter(i => i.kind === 'Import');
  if (imports.length > 1) {
    out.push({ startLine: analysis.lineMap.lineOf(imports[0].start), endLine: analysis.lineMap.lineOf(imports[imports.length - 1].start), kind: FoldingRangeKind.Imports });
  }
  return out;
}
