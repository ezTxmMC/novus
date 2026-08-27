/** Context-aware completion for Novus documents. */
import {
  CompletionItem,
  CompletionItemKind,
  CompletionItemTag,
  InsertTextFormat,
  MarkupKind,
} from 'vscode-languageserver/node';
import { Analysis, Scope } from './analyzer';
import { findNodePath } from './ast';
import { BUILTIN_ANNOTATIONS, KEYWORDS, KEYWORD_MAP, PRIMITIVE_TYPES } from './builtins';
import { RESERVED_WORDS } from './lexer';
import { createImportEdit } from './imports';
import { CLASS_LIKE, NSymbol, describe, mkType, typeToString } from './symbols';
import { Workspace } from './workspace';

const MODIFIERS = ['private', 'public', 'protected', 'final', 'static', 'abstract'];
const STATEMENT_KEYWORDS = ['var', 'println', 'return', 'if', 'else', 'for', 'while', 'break', 'continue'];
const TOP_LEVEL_KEYWORDS = ['package', 'import', 'method', 'define', 'var', 'println', 'private', 'final'];
const CLASS_BODY_KEYWORDS = ['method', 'construct', 'define', 'abstract', ...MODIFIERS];
const EXPRESSION_KEYWORDS = ['this', 'true', 'false', 'null'];

type Context = 'toplevel' | 'class' | 'statement' | 'expression';

interface Snippet {
  label: string;
  detail: string;
  body: string;
  contexts: Context[];
}

const SNIPPETS: Snippet[] = [
  { label: 'method', detail: 'method … { }', body: 'method ${1:name}(${2}) {\n\t$0\n}', contexts: ['toplevel', 'class'] },
  { label: 'main', detail: 'method main { }', body: 'method main {\n\t$0\n}', contexts: ['toplevel'] },
  { label: 'define class', detail: 'define class … { }', body: 'define class ${1:Name} {\n\t$0\n}', contexts: ['toplevel'] },
  { label: 'define enum', detail: 'define enum … { }', body: 'define enum ${1:Name} {\n\t${2:FIRST}, ${3:SECOND};\n\t$0\n}', contexts: ['toplevel'] },
  { label: 'define interface', detail: 'define interface … { }', body: 'define interface ${1:IName} {\n\t$0\n}', contexts: ['toplevel'] },
  { label: 'define abstract', detail: 'define abstract … { }', body: 'define abstract ${1:Name} {\n\t$0\n}', contexts: ['toplevel'] },
  { label: 'define annotation', detail: 'define annotation … { }', body: 'define annotation ${1:Name} {\n\t${2:text}(): string\n}', contexts: ['toplevel'] },
  { label: 'construct', detail: 'construct(…) { }', body: 'construct(${1}) {\n\t$0\n}', contexts: ['class'] },
  { label: 'field', detail: 'private final Type name: get, set', body: 'private final ${1:string} ${2:name}: ${3:get, set}', contexts: ['class'] },
  { label: 'if', detail: 'if (…) { }', body: 'if (${1:condition}) {\n\t$0\n}', contexts: ['statement'] },
  { label: 'if else', detail: 'if (…) { } else { }', body: 'if (${1:condition}) {\n\t$2\n} else {\n\t$0\n}', contexts: ['statement'] },
  { label: 'for', detail: 'for (item in items) { }', body: 'for (${1:item} in ${2:items}) {\n\t$0\n}', contexts: ['statement'] },
  { label: 'while', detail: 'while (…) { }', body: 'while (${1:condition}) {\n\t$0\n}', contexts: ['statement'] },
  { label: 'var', detail: 'var name = value', body: 'var ${1:name} = ${2:value}', contexts: ['statement', 'toplevel'] },
  { label: 'println', detail: 'println "…"', body: 'println "${1:text}"', contexts: ['statement', 'toplevel'] },
];

export function complete(ws: Workspace, analysis: Analysis, offset: number): CompletionItem[] {
  const text = analysis.text;
  const lineStart = text.lastIndexOf('\n', offset - 1) + 1;
  const before = text.slice(lineStart, offset);
  const scope = analysis.scopeAt(offset);

  if (inComment(text, offset) || inString(text, offset)) return [];

  // @Annotation
  if (/@\w*$/.test(before)) return annotationItems(ws, analysis);

  // field names inside `Type{ … }` / `@Annotation{ … }`
  if (/(?:^|\{|,)\s*\w*$/.test(before)) {
    const init = initializerFieldItems(analysis, scope, offset);
    if (init) return init;
  }

  // package / import
  if (/\bpackage\s+[\w.]*$/.test(before)) return [];
  if (/\bimport\s+[\w.]*$/.test(before)) return moduleItems(ws, analysis);

  // based X, Y
  if (/\bbased\s+[\w,\s]*$/.test(before)) return typeItems(ws, analysis, offset, 'class');

  // member access: a.b().c.
  const chain = /((?:[A-Za-z_]\w*(?:\(\))?\.)*[A-Za-z_]\w*(?:\(\))?)\s*\.\s*\w*$/.exec(before);
  if (chain) return memberItems(ws, analysis, scope, chain[1], offset, before);
  if (/\)\s*\.\s*\w*$/.test(before) || /\]\s*\.\s*\w*$/.test(before)) return memberItemsFromAst(ws, analysis, scope, offset, before);

  // type positions
  if (/\)\s*:\s*\w*$/.test(before)) return typeItems(ws, analysis, offset, 'all', true);
  if (/\bvar\s+\w+\s*:\s*\w*$/.test(before)) return typeItems(ws, analysis, offset, 'all');
  const fieldAccessor = /^\s*(?:(?:private|public|protected|final|static|abstract)\s+)*[A-Za-z_]\w*(?:<[^>]*>)?\s+[A-Za-z_]\w*\s*:\s*(?:(?:get|set)\s*,\s*)*\w*$/;
  if (fieldAccessor.test(before)) return accessorItems();
  if (/<\s*\w*$/.test(before) && /\b[A-Za-z_]\w*\s*<\s*\w*$/.test(before)) return typeItems(ws, analysis, offset, 'all');
  if (inDeclarationParams(before)) return typeItems(ws, analysis, offset, 'all');
  if (/\b(define)\s+\w*$/.test(before)) return ['class', 'enum', 'interface', 'abstract', 'annotation'].map(k => keywordItem(k, '1'));
  if (/\bdefine\s+(class|enum|interface|abstract|annotation)\s+\w*$/.test(before)) return [];
  if (/\bmethod\s+\w*$/.test(before)) return [];

  const context = contextAt(analysis, offset, before);
  const items: CompletionItem[] = [];

  switch (context) {
    case 'toplevel':
      TOP_LEVEL_KEYWORDS.forEach(k => items.push(keywordItem(k, '3')));
      items.push(...typeItems(ws, analysis, offset, 'all').map(i => ({ ...i, sortText: '5' + i.label })));
      items.push(...symbolItems(analysis, ws, offset, scope, { valuesOnly: false }));
      break;
    case 'class':
      CLASS_BODY_KEYWORDS.forEach(k => items.push(keywordItem(k, '3')));
      items.push(...typeItems(ws, analysis, offset, 'all').map(i => ({ ...i, sortText: '4' + i.label })));
      break;
    case 'statement':
      STATEMENT_KEYWORDS.forEach(k => items.push(keywordItem(k, '3')));
      EXPRESSION_KEYWORDS.forEach(k => items.push(keywordItem(k, '3')));
      items.push(...symbolItems(analysis, ws, offset, scope, { valuesOnly: false }));
      break;
    case 'expression':
      EXPRESSION_KEYWORDS.forEach(k => items.push(keywordItem(k, '3')));
      items.push(...symbolItems(analysis, ws, offset, scope, { valuesOnly: false }));
      break;
  }

  for (const s of SNIPPETS) {
    if (!s.contexts.includes(context)) continue;
    items.push({
      label: s.label,
      kind: CompletionItemKind.Snippet,
      detail: s.detail,
      insertText: s.body,
      insertTextFormat: InsertTextFormat.Snippet,
      sortText: '6' + s.label,
    });
  }
  return dedupe(items);
}

// -------------------------------------------------------------------- helpers

function inComment(text: string, offset: number): boolean {
  const lineStart = text.lastIndexOf('\n', offset - 1) + 1;
  const line = text.slice(lineStart, offset);
  if (line.includes('//')) return true;
  const open = text.lastIndexOf('/*', offset);
  if (open < 0) return false;
  const close = text.indexOf('*/', open + 2);
  return close < 0 || close >= offset;
}

function inString(text: string, offset: number): boolean {
  const lineStart = text.lastIndexOf('\n', offset - 1) + 1;
  let inStr = false;
  let interpDepth = 0;
  for (let i = lineStart; i < offset; i++) {
    const c = text[i];
    if (inStr) {
      if (c === '\\') {
        i++;
      } else if (c === '$' && text[i + 1] === '{') {
        interpDepth = 1;
        i++;
        while (i + 1 < offset && interpDepth > 0) {
          i++;
          if (text[i] === '{') interpDepth++;
          else if (text[i] === '}') interpDepth--;
        }
        if (interpDepth > 0) return false; // inside ${ … } → expression context
      } else if (c === '"') {
        inStr = false;
      }
    } else if (c === '"') {
      inStr = true;
    }
  }
  return inStr;
}

function inDeclarationParams(before: string): boolean {
  const m = /\b(?:method\s+\w+|construct|define\s+(?:class|enum)\s+\w+)\s*\(([^)]*)$/.exec(before);
  if (!m) return false;
  const inside = m[1];
  // Type position: right after '(' or ','; name position after `Type `
  return /(?:^|,)\s*\w*$/.test(inside);
}

function contextAt(analysis: Analysis, offset: number, before: string): Context {
  const path = findNodePath(analysis.program, offset);
  let context: Context = 'toplevel';
  for (const node of path) {
    if (node.kind === 'Define' && node.bodySpan && node.bodySpan.start < offset && offset <= node.bodySpan.end) context = 'class';
    if (node.kind === 'Method' && node.body && node.body.start < offset && offset <= node.body.end) context = 'statement';
    if (node.kind === 'Block' && context !== 'class') context = 'statement';
  }
  if (context === 'toplevel' || context === 'statement') {
    const atStart = /^\s*\w*$/.test(before);
    if (!atStart) {
      const trimmed = before.trimEnd();
      if (/(?:=|\(|,|\[|\{|:|\+|-|\*|\/|%|<|>|!|&&|\|\||\breturn|\bprintln|\bin)\s*\w*$/.test(before) || /[=(,[{+\-*/%<>!]$/.test(trimmed)) {
        return 'expression';
      }
      return 'expression';
    }
  }
  if (context === 'class') {
    const atStart = /^\s*(?:(?:private|public|protected|final|static|abstract)\s+)*\w*$/.test(before);
    if (!atStart) return 'expression';
  }
  return context;
}

function keywordItem(name: string, sort: string): CompletionItem {
  const doc = KEYWORD_MAP.get(name);
  return {
    label: name,
    kind: CompletionItemKind.Keyword,
    detail: doc?.detail,
    documentation: doc ? { kind: MarkupKind.Markdown, value: doc.doc } : undefined,
    sortText: sort + name,
  };
}

function accessorItems(): CompletionItem[] {
  return ['get', 'set'].map(k => keywordItem(k, '0'));
}

function annotationItems(ws: Workspace, analysis: Analysis): CompletionItem[] {
  const items: CompletionItem[] = [];
  const seen = new Set<string>();
  const add = (name: string, detail: string, doc: string | undefined, args: string[]): void => {
    if (seen.has(name)) return;
    seen.add(name);
    const item: CompletionItem = { label: name, kind: CompletionItemKind.Interface, detail, sortText: '1' + name };
    if (doc) item.documentation = { kind: MarkupKind.Markdown, value: doc };
    if (args.length) {
      item.insertText = `${name}{ ${args.map((a, i) => `${a}=\${${i + 1}}`).join(', ')} }`;
      item.insertTextFormat = InsertTextFormat.Snippet;
    }
    items.push(item);
  };
  const userAnnotations = [...analysis.symbols, ...ws.globalSymbols(analysis.uri)].filter(s => s.kind === 'annotation');
  for (const a of userAnnotations) {
    add(a.name, describe(a), a.doc, a.members.filter(m => m.kind === 'method').map(m => m.name));
    if (!analysis.isVisible(a)) {
      const pkg = analysis.packageFor(a);
      const item = items[items.length - 1];
      if (pkg && item.label === a.name) withImport(item, analysis, pkg);
    }
  }
  for (const b of BUILTIN_ANNOTATIONS) add(b.name, `@${b.name}`, b.doc, b.args.map(a => a.name));
  return items;
}

function moduleItems(ws: Workspace, analysis: Analysis): CompletionItem[] {
  const items: CompletionItem[] = [];
  for (const m of ws.builtins) {
    items.push({ label: m.name, kind: CompletionItemKind.Module, detail: describe(m), documentation: m.doc ? { kind: MarkupKind.Markdown, value: m.doc } : undefined, sortText: '1' + m.name });
  }
  for (const p of ws.packages(analysis.uri)) {
    if (analysis.importNames.includes(p)) continue;
    items.push({ label: p, kind: CompletionItemKind.Module, detail: `package ${p}`, sortText: '2' + p });
  }
  return items;
}

function typeItems(ws: Workspace, analysis: Analysis, offset: number, filter: 'all' | 'class', includeVoid = false): CompletionItem[] {
  const items: CompletionItem[] = [];
  if (filter === 'all') {
    for (const t of PRIMITIVE_TYPES) {
      if (t.name === 'void' && !includeVoid) continue;
      items.push({
        label: t.name,
        kind: CompletionItemKind.TypeParameter,
        detail: t.implemented ? 'primitive type' : 'primitive type (concept)',
        documentation: { kind: MarkupKind.Markdown, value: t.doc },
        insertText: t.generic ? `${t.name}<$1>` : t.name,
        insertTextFormat: t.generic ? InsertTextFormat.Snippet : InsertTextFormat.PlainText,
        sortText: (t.implemented ? '0' : '1') + t.name,
      });
    }
  }
  const globals = ws.globalSymbols(analysis.uri);
  const visible = analysis.visibleSymbols(offset, globals.filter(g => analysis.isVisible(g)));
  const names = new Set<string>();
  for (const s of visible) {
    if (!CLASS_LIKE.has(s.kind)) continue;
    if (filter === 'class' && s.kind === 'annotation') continue;
    names.add(s.name);
    items.push(symbolItem(s, '2', false));
  }
  for (const s of importableSymbols(analysis, globals, names)) {
    if (!CLASS_LIKE.has(s.kind)) continue;
    if (filter === 'class' && s.kind === 'annotation') continue;
    items.push(withImport(symbolItem(s, '7', false), analysis, analysis.packageFor(s)!));
  }
  return items;
}

/** Global symbols that need an import, excluding names already visible. */
function importableSymbols(analysis: Analysis, globals: NSymbol[], visibleNames: Set<string>): NSymbol[] {
  const out: NSymbol[] = [];
  const seen = new Set<string>();
  for (const g of globals) {
    if (analysis.isVisible(g) || visibleNames.has(g.name) || g.kind === 'package' || !g.name) continue;
    const pkg = analysis.packageFor(g);
    if (!pkg) continue;
    const key = `${g.kind}:${g.name}:${(g.params ?? []).length}:${pkg}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push(g);
  }
  return out;
}

/** Marks a completion item as auto-importing `pkg` when accepted. */
function withImport(item: CompletionItem, analysis: Analysis, pkg: string): CompletionItem {
  item.additionalTextEdits = [createImportEdit(analysis, [pkg])];
  item.labelDetails = { ...(item.labelDetails ?? {}), description: `import ${pkg}` };
  item.detail = `${item.detail ?? item.label}  (auto-import from '${pkg}')`;
  item.sortText = '7' + item.label;
  return item;
}

function memberItems(ws: Workspace, analysis: Analysis, scope: Scope, chainText: string, offset: number, before: string): CompletionItem[] {
  const segments = chainText.split('.').map(s => ({ name: s.replace(/\(\)$/, ''), call: s.endsWith('()') }));
  const first = segments[0];
  let members: NSymbol[] | undefined;

  const membersOfValue = (type: ReturnType<Analysis['typeOf']>): NSymbol[] => analysis.membersOfType(type, scope);

  let currentType: ReturnType<Analysis['typeOf']> | undefined;
  if (first.name === 'this') {
    const cls = analysis.enclosingClass(scope);
    if (!cls) return [];
    currentType = mkType(cls.name);
  } else {
    const syms = analysis.lookup(first.name, scope);
    const sym = first.call ? syms.find(s => s.kind === 'method' || CLASS_LIKE.has(s.kind)) ?? syms[0] : syms[0];
    if (!sym) return memberItemsFromAst(ws, analysis, scope, offset, before);
    currentType = first.call ? (CLASS_LIKE.has(sym.kind) ? mkType(sym.name) : sym.returnType) : analysis.typeOfSymbol(sym);
  }

  for (let i = 1; i < segments.length; i++) {
    const seg = segments[i];
    const list = membersOfValue(currentType);
    const candidates = list.filter(m => m.name === seg.name);
    const member = seg.call ? candidates.find(m => m.kind === 'method') ?? candidates[0] : candidates.find(m => m.kind !== 'method') ?? candidates[0];
    if (!member) return [];
    currentType = seg.call ? member.returnType ?? (member.accessorOf ? member.accessorOf.type : undefined) : analysis.typeOfSymbol(member);
  }

  members = membersOfValue(currentType);
  return memberCompletionItems(members);
}

function memberItemsFromAst(_ws: Workspace, analysis: Analysis, scope: Scope, offset: number, before: string): CompletionItem[] {
  // Find the Member node whose name is being typed (parser produces one even for `foo.`).
  const dot = before.lastIndexOf('.');
  if (dot < 0) return [];
  const lineStart = offset - before.length;
  const dotOffset = lineStart + dot;
  const path = findNodePath(analysis.program, dotOffset);
  for (let i = path.length - 1; i >= 0; i--) {
    const node = path[i];
    if (node.kind === 'Member' && node.object.end <= dotOffset) {
      const type = analysis.typeOf(node.object, scope);
      return memberCompletionItems(analysis.membersOfType(type, scope));
    }
  }
  return [];
}

function memberCompletionItems(members: NSymbol[]): CompletionItem[] {
  const items: CompletionItem[] = [];
  const seen = new Set<string>();
  for (const m of members) {
    if (m.kind === 'constructor' || !m.name) continue;
    const key = `${m.kind}:${m.name}:${(m.params ?? []).length}`;
    if (seen.has(key)) continue;
    seen.add(key);
    items.push(symbolItem(m, m.kind === 'field' || m.kind === 'enumMember' ? '0' : '1', true));
  }
  return items;
}

function symbolItems(analysis: Analysis, ws: Workspace, offset: number, _scope: Scope, _o: { valuesOnly: boolean }): CompletionItem[] {
  const items: CompletionItem[] = [];
  const globals = ws.globalSymbols(analysis.uri);
  const visible = analysis.visibleSymbols(offset, globals.filter(g => analysis.isVisible(g)));
  const names = new Set<string>();
  for (const s of visible) {
    if (s.kind === 'package' || s.kind === 'constructor' || !s.name) continue;
    names.add(s.name);
    let sort = '2';
    if (s.local || s.kind === 'parameter') sort = '0';
    else if (s.kind === 'field' || (s.container && CLASS_LIKE.has(s.container.kind))) sort = '1';
    else if (s.builtin || s.uri !== analysis.uri) sort = '5';
    items.push(symbolItem(s, sort, true));
    if (s.kind === 'class') items.push(structInitItem(analysis, s, sort));
  }
  for (const s of importableSymbols(analysis, globals, names)) {
    if (s.kind === 'constructor') continue;
    const pkg = analysis.packageFor(s)!;
    items.push(withImport(symbolItem(s, '7', true), analysis, pkg));
    if (s.kind === 'class') items.push(withImport(structInitItem(analysis, s, '7'), analysis, pkg));
  }
  return items;
}

// ------------------------------------------------------- object initializers

/** Fields that an object initializer `Type{ … }` can set: own fields first, then inherited. */
function initializableFields(analysis: Analysis, cls: NSymbol): NSymbol[] {
  const out: NSymbol[] = [];
  const seen = new Set<string>();
  for (const m of analysis.membersOf(cls)) {
    if (m.kind !== 'field' || !m.name || m.modifiers.includes('static') || seen.has(m.name)) continue;
    seen.add(m.name);
    out.push(m);
  }
  return out;
}

/** `Key{…}` completion item expanding to a snippet with one placeholder per field. */
function structInitItem(analysis: Analysis, cls: NSymbol, sort: string): CompletionItem {
  const fields = initializableFields(analysis, cls);
  const placeholders = fields.map((f, i) => `${f.name}=\${${i + 1}}`);
  let body: string;
  if (!fields.length) body = `${cls.name}{$1}`;
  else if (fields.length > 2) body = `${cls.name}{\n\t${placeholders.join(',\n\t')}\n}`;
  else body = `${cls.name}{${placeholders.join(', ')}}`;
  const summary = fields.map(f => `${f.name}=${typeToString(f.type)}`).join(', ');
  return {
    label: `${cls.name}{…}`,
    filterText: cls.name,
    kind: CompletionItemKind.Constructor,
    detail: `${cls.name}{${summary}}`,
    documentation: {
      kind: MarkupKind.Markdown,
      value: `Initialize a \`${cls.name}\` with named fields.${cls.doc ? '\n\n' + cls.doc : ''}`,
    },
    insertText: body,
    insertTextFormat: InsertTextFormat.Snippet,
    sortText: sort + cls.name + '{',
  };
}

interface InitializerContext {
  brace: number;
  name: string;
  annotation: boolean;
  existing: Set<string>;
}

/** Finds the innermost unclosed `Name{` (or `@Name{`) enclosing `offset`. */
function findInitializer(text: string, offset: number): InitializerContext | undefined {
  let depth = 0;
  let i = offset - 1;
  let brace = -1;
  while (i >= 0) {
    const c = text[i];
    if (c === '"') {
      i--;
      while (i >= 0 && !(text[i] === '"' && text[i - 1] !== '\\')) i--;
      i--;
      continue;
    }
    if (c === ')' || c === ']' || c === '}') depth++;
    else if (c === '(' || c === '[' || c === '{') {
      if (depth === 0) {
        if (c === '{') brace = i;
        break;
      }
      depth--;
    }
    i--;
  }
  if (brace < 0) return undefined;
  const head = text.slice(0, brace);
  const m = /([A-Za-z_]\w*)\s*$/.exec(head);
  if (!m || RESERVED_WORDS.has(m[1])) return undefined;
  const beforeName = head.slice(0, head.length - m[0].length);
  const annotation = /@\s*$/.test(beforeName);
  // A struct initializer only appears in expression position; `define class X {`, `based X {`,
  // `method x {` etc. are excluded here and by the type lookup in the caller.
  if (!annotation && !/(^|[=(\[,:{;]|\breturn|\bprintln|\bin)\s*$/.test(beforeName)) return undefined;

  const existing = new Set<string>();
  const inner = text.slice(brace + 1, offset);
  let d = 0;
  for (let j = 0; j < inner.length; j++) {
    const c = inner[j];
    if (c === '"') {
      j++;
      while (j < inner.length && inner[j] !== '"') {
        if (inner[j] === '\\') j++;
        j++;
      }
      continue;
    }
    if (c === '(' || c === '[' || c === '{') d++;
    else if (c === ')' || c === ']' || c === '}') d--;
    else if (d === 0 && /[A-Za-z_]/.test(c) && (j === 0 || !/\w/.test(inner[j - 1]))) {
      const k = /^([A-Za-z_]\w*)\s*=(?!=)/.exec(inner.slice(j));
      if (k) {
        existing.add(k[1]);
        j += k[0].length - 1;
      }
    }
  }
  return { brace, name: m[1], annotation, existing };
}

/** Completion of field / argument names inside `Type{ … }` and `@Annotation{ … }`. */
function initializerFieldItems(analysis: Analysis, scope: Scope, offset: number): CompletionItem[] | undefined {
  const ctx = findInitializer(analysis.text, offset);
  if (!ctx) return undefined;
  const items: CompletionItem[] = [];
  const add = (name: string, type: string, doc: string | undefined, kind: CompletionItemKind): void => {
    if (ctx.existing.has(name)) return;
    items.push({
      label: name,
      kind,
      detail: `${type} ${name}`,
      documentation: doc ? { kind: MarkupKind.Markdown, value: doc } : undefined,
      insertText: `${name}=`,
      sortText: '0' + name,
      command: { title: 'Suggest', command: 'editor.action.triggerSuggest' },
    });
  };

  if (ctx.annotation) {
    const sym = analysis.lookupType(ctx.name, scope);
    if (sym && sym.kind === 'annotation') {
      for (const m of analysis.membersOf(sym)) {
        if (m.kind === 'method' && !m.accessorOf) add(m.name, typeToString(m.returnType), m.doc, CompletionItemKind.Property);
      }
      return items;
    }
    const builtin = BUILTIN_ANNOTATIONS.find(b => b.name === ctx.name);
    if (builtin) {
      for (const a of builtin.args) add(a.name, a.type, a.doc, CompletionItemKind.Property);
      return items;
    }
    return undefined;
  }

  const cls = analysis.lookupType(ctx.name, scope);
  if (!cls || !CLASS_LIKE.has(cls.kind) || cls.kind === 'enum' || cls.kind === 'interface' || cls.kind === 'annotation') return undefined;
  for (const f of initializableFields(analysis, cls)) add(f.name, typeToString(f.type), f.doc, CompletionItemKind.Field);
  if (!analysis.isVisible(cls)) {
    const pkg = analysis.packageFor(cls);
    if (pkg) for (const item of items) withImport(item, analysis, pkg);
  }
  return items;
}

function symbolItem(s: NSymbol, sort: string, callSnippet: boolean): CompletionItem {
  const item: CompletionItem = {
    label: s.name,
    kind: completionKind(s),
    detail: describe(s),
    sortText: sort + s.name,
    documentation: s.doc ? { kind: MarkupKind.Markdown, value: s.doc } : undefined,
  };
  if (s.deprecated !== undefined) item.tags = [CompletionItemTag.Deprecated];
  if (callSnippet && (s.kind === 'method' || s.kind === 'constructor')) {
    const params = s.params ?? [];
    item.label = s.name;
    item.labelDetails = { detail: `(${params.map(p => (p.type ? typeToString(p.type) + ' ' : '') + p.name).join(', ')})`, description: s.returnType ? typeToString(s.returnType) : undefined };
    item.insertText = params.length ? `${s.name}($1)` : `${s.name}()`;
    item.insertTextFormat = InsertTextFormat.Snippet;
    if (params.length) item.command = { title: 'Trigger parameter hints', command: 'editor.action.triggerParameterHints' };
  } else if (s.kind === 'field' || s.kind === 'variable' || s.kind === 'constant' || s.kind === 'parameter') {
    item.labelDetails = { description: typeToString(s.type) === 'unknown' ? undefined : typeToString(s.type) };
  }
  return item;
}

function completionKind(s: NSymbol): CompletionItemKind {
  switch (s.kind) {
    case 'package':
    case 'module':
      return CompletionItemKind.Module;
    case 'class':
    case 'abstract':
      return CompletionItemKind.Class;
    case 'enum':
      return CompletionItemKind.Enum;
    case 'interface':
    case 'annotation':
      return CompletionItemKind.Interface;
    case 'method':
      return s.container && CLASS_LIKE.has(s.container.kind) ? CompletionItemKind.Method : CompletionItemKind.Function;
    case 'constructor':
      return CompletionItemKind.Constructor;
    case 'field':
      return CompletionItemKind.Field;
    case 'constant':
      return CompletionItemKind.Constant;
    case 'variable':
    case 'parameter':
      return CompletionItemKind.Variable;
    case 'enumMember':
      return CompletionItemKind.EnumMember;
    case 'type':
      return CompletionItemKind.TypeParameter;
  }
}

function dedupe(items: CompletionItem[]): CompletionItem[] {
  const seen = new Set<string>();
  const out: CompletionItem[] = [];
  for (const item of items) {
    const key = `${item.kind}:${item.label}:${item.labelDetails?.detail ?? ''}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push(item);
  }
  return out;
}

export { KEYWORDS };
