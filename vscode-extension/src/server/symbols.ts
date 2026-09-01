/** Symbol model shared by the analyzer, builtins and the LSP features. */
import * as ast from './ast';
import { Span } from './lexer';

export type SymbolKind =
  | 'package'
  | 'module'
  | 'class'
  | 'enum'
  | 'interface'
  | 'abstract'
  | 'annotation'
  | 'method'
  | 'constructor'
  | 'field'
  | 'constant'
  | 'variable'
  | 'parameter'
  | 'enumMember'
  | 'type';

export interface NSymbol {
  /** Stable identity: `${uri}#${selectionSpan.start}#${name}` (or `builtin#...`). */
  id: string;
  kind: SymbolKind;
  name: string;
  uri: string;
  /** Full extent of the declaration. */
  span: Span;
  /** The name token. */
  selectionSpan: Span;
  type?: ast.TypeRef;
  returnType?: ast.TypeRef;
  params?: ast.Param[];
  modifiers: string[];
  annotations: string[];
  accessors: string[];
  doc?: string;
  container?: NSymbol;
  members: NSymbol[];
  bases: ast.TypeRef[];
  builtin: boolean;
  deprecated?: string;
  /** Set when at least one reference (other than the declaration) was seen. */
  referenced?: boolean;
  /** Field this synthesized getter/setter belongs to. */
  accessorOf?: NSymbol;
  /** Declaration node (for lazy type inference). */
  decl?: ast.Node;
  /** True for `define class X(...)` synthesized constructors and similar. */
  synthesized?: boolean;
  /** Only meaningful for locals: declared inside a method body. */
  local?: boolean;
}

export const CLASS_LIKE: ReadonlySet<SymbolKind> = new Set<SymbolKind>(['class', 'enum', 'interface', 'abstract', 'annotation']);
export const TYPE_LIKE: ReadonlySet<SymbolKind> = new Set<SymbolKind>(['class', 'enum', 'interface', 'abstract', 'annotation', 'module', 'type']);
export const VALUE_LIKE: ReadonlySet<SymbolKind> = new Set<SymbolKind>(['field', 'constant', 'variable', 'parameter', 'enumMember']);

let builtinCounter = 0;

export function newSymbol(init: Partial<NSymbol> & { kind: SymbolKind; name: string; uri: string }): NSymbol {
  const selectionSpan = init.selectionSpan ?? init.span ?? { start: 0, end: 0 };
  const span = init.span ?? selectionSpan;
  const builtin = init.builtin ?? false;
  const id = init.id ?? (builtin ? `builtin#${builtinCounter++}#${init.name}` : `${init.uri}#${selectionSpan.start}#${init.name}`);
  return {
    id,
    kind: init.kind,
    name: init.name,
    uri: init.uri,
    span,
    selectionSpan,
    type: init.type,
    returnType: init.returnType,
    params: init.params,
    modifiers: init.modifiers ?? [],
    annotations: init.annotations ?? [],
    accessors: init.accessors ?? [],
    doc: init.doc,
    container: init.container,
    members: init.members ?? [],
    bases: init.bases ?? [],
    builtin,
    deprecated: init.deprecated,
    referenced: init.referenced,
    accessorOf: init.accessorOf,
    decl: init.decl,
    synthesized: init.synthesized,
    local: init.local,
  };
}

export function mkType(name: string, args: ast.TypeRef[] = []): ast.TypeRef {
  return { kind: 'TypeRef', name, nameSpan: { start: -1, end: -1 }, args, start: -1, end: -1 };
}

export function mkParam(typeName: string, name: string, typeArgs: ast.TypeRef[] = []): ast.Param {
  return { kind: 'Param', type: mkType(typeName, typeArgs), name, nameSpan: { start: -1, end: -1 }, start: -1, end: -1 };
}

export function typeToString(t?: ast.TypeRef): string {
  if (!t || !t.name) return 'unknown';
  if (!t.args.length) return t.name;
  return `${t.name}<${t.args.map(typeToString).join(', ')}>`;
}

export function paramsToString(params?: ast.Param[]): string {
  if (!params) return '';
  return params.map(p => (p.type ? `${typeToString(p.type)} ${p.name}` : p.name)).join(', ');
}

export function sameType(a?: ast.TypeRef, b?: ast.TypeRef): boolean {
  if (!a || !b) return !a && !b;
  if (a.name !== b.name || a.args.length !== b.args.length) return false;
  return a.args.every((x, i) => sameType(x, b.args[i]));
}

/** Short one-line signature used in hovers, completion details and the outline. */
export function describe(sym: NSymbol): string {
  const mods = sym.modifiers.length ? sym.modifiers.join(' ') + ' ' : '';
  switch (sym.kind) {
    case 'package':
      return `package ${sym.name}`;
    case 'module':
      return `module ${sym.name}`;
    case 'class':
    case 'enum':
    case 'interface':
    case 'abstract':
    case 'annotation': {
      const bases = sym.bases.length ? ` based ${sym.bases.map(typeToString).join(', ')}` : '';
      return `${mods}define ${sym.kind} ${sym.name}${bases}`;
    }
    case 'method': {
      const ret = sym.returnType ? `: ${typeToString(sym.returnType)}` : '';
      const owner = sym.container && CLASS_LIKE.has(sym.container.kind) ? `${sym.container.name}.` : '';
      if (sym.accessorOf) {
        return `${owner}${sym.name}(${paramsToString(sym.params)})${ret}`;
      }
      return `${mods}method ${owner}${sym.name}(${paramsToString(sym.params)})${ret}`;
    }
    case 'constructor': {
      const owner = sym.container ? sym.container.name : '';
      return `${mods}construct ${owner}(${paramsToString(sym.params)})`;
    }
    case 'field': {
      const acc = sym.accessors.length ? `: ${sym.accessors.join(', ')}` : '';
      const owner = sym.container ? `${sym.container.name}.` : '';
      return `${mods}${typeToString(sym.type)} ${owner}${sym.name}${acc}`;
    }
    case 'constant':
      return `${mods}${sym.name}: ${typeToString(sym.type)}`;
    case 'variable':
      return `var ${sym.name}: ${typeToString(sym.type)}`;
    case 'parameter':
      return `(parameter) ${typeToString(sym.type)} ${sym.name}`;
    case 'enumMember':
      return `${sym.container ? sym.container.name + '.' : ''}${sym.name}`;
    case 'type':
      return `type ${sym.name}`;
  }
}

/** Maps a symbol to an LSP semantic token type. */
export function semanticTokenType(sym: NSymbol): string {
  switch (sym.kind) {
    case 'package':
    case 'module':
      return 'namespace';
    case 'class':
    case 'abstract':
      return 'class';
    case 'enum':
      return 'enum';
    case 'interface':
      return 'interface';
    case 'annotation':
      return 'decorator';
    case 'method':
    case 'constructor':
      return sym.container && CLASS_LIKE.has(sym.container.kind) ? 'method' : 'function';
    case 'field':
      return 'property';
    case 'constant':
    case 'variable':
      return 'variable';
    case 'parameter':
      return 'parameter';
    case 'enumMember':
      return 'enumMember';
    case 'type':
      return 'type';
  }
}

export function semanticTokenModifiers(sym: NSymbol): string[] {
  const out: string[] = [];
  if (sym.modifiers.includes('final') || sym.kind === 'constant' || sym.kind === 'enumMember') out.push('readonly');
  if (sym.modifiers.includes('static')) out.push('static');
  if (sym.modifiers.includes('abstract') || sym.kind === 'abstract') out.push('abstract');
  if (sym.builtin) out.push('defaultLibrary');
  if (sym.deprecated !== undefined) out.push('deprecated');
  return out;
}
