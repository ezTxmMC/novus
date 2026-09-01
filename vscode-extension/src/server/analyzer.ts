/**
 * Semantic analysis: symbol tables, scopes, name resolution, light type
 * inference and diagnostics for a single Novus document.
 */
import * as fs from 'fs';
import * as path from 'path';
import { URI } from 'vscode-uri';
import { isManifestUri, parseManifestText } from './project';
import * as ast from './ast';
import { BUILTIN_ANNOTATIONS, builtinGlobals, builtinMembers, enumBuiltinMembers, isBoolType, isFloatType, isPrimitive, isStringType } from './builtins';
import { Comment, Span } from './lexer';
import { LineMap, ParseError, parse } from './parser';
import { CLASS_LIKE, NSymbol, TYPE_LIKE, mkType, newSymbol, sameType, semanticTokenModifiers, semanticTokenType, typeToString } from './symbols';

export type ScopeKind = 'module' | 'class' | 'method' | 'block';

export interface Scope {
  kind: ScopeKind;
  span: Span;
  parent?: Scope;
  symbols: Map<string, NSymbol[]>;
  owner?: NSymbol;
}

export interface Reference {
  span: Span;
  symbol?: NSymbol;
  tokenType: string;
  modifiers: string[];
  declaration: boolean;
}

export type Severity = 'error' | 'warning' | 'info' | 'hint';

export interface NDiagnostic {
  span: Span;
  message: string;
  severity: Severity;
  tags?: ('unnecessary' | 'deprecated')[];
  /** Machine readable code, e.g. `missing-import`. */
  code?: string;
  data?: unknown;
}

export interface MissingImportData {
  name: string;
  packages: string[];
}

export interface AnalyzeOptions {
  /** Top-level symbols declared in other files (all packages, imported or not). */
  globalLookup(name: string, excludeUri: string): NSymbol[];
  /** Package declared by the file at `uri` ('' when it has none). */
  packageOf(uri: string): string;
  /** Builtin module (`http`, `json`, …) by name. */
  builtinModule(name: string): NSymbol | undefined;
  /** Resolves a module import of the enclosing project to a file URI (and indexes it). */
  resolveModuleImport?(fromUri: string, rel: string): string | undefined;
  undefinedSymbols: boolean;
  unusedVariables: boolean;
}

export class Analysis {
  /** True for project.nv manifests (no Novus code inside). */
  readonly isManifest: boolean;
  readonly program: ast.Program;
  readonly parseErrors: ParseError[];
  readonly comments: Comment[];
  readonly lineMap: LineMap;
  readonly diagnostics: NDiagnostic[] = [];
  readonly references: Reference[] = [];
  /** Top-level symbols declared in this file. */
  readonly symbols: NSymbol[] = [];
  readonly allSymbols: NSymbol[] = [];
  readonly scopes: Scope[] = [];
  readonly moduleScope: Scope;
  packageName?: string;
  readonly imports: NSymbol[] = [];
  /** Full names of imported packages/modules. */
  readonly importNames: string[] = [];

  private readonly declScopes = new Map<ast.Node, Scope>();
  private readonly declSymbols = new Map<ast.Node, NSymbol>();
  private readonly inferring = new Set<NSymbol>();

  constructor(
    readonly uri: string,
    readonly text: string,
    private readonly opts: AnalyzeOptions,
  ) {
    this.isManifest = isManifestUri(uri);
    const parsed = parse(this.isManifest ? '' : text);
    this.program = parsed.program;
    this.parseErrors = parsed.errors;
    this.comments = parsed.comments;
    this.lineMap = this.isManifest ? new LineMap(text) : parsed.lineMap;
    this.moduleScope = this.newScope('module', { start: 0, end: text.length });

    for (const e of parsed.errors) {
      this.diagnostics.push({ span: e, message: e.message, severity: 'error' });
    }
    if (this.isManifest) {
      // project.nv: validate the manifest syntax instead of parsing Novus code
      const manifest = parseManifestText(URI.parse(uri).fsPath, text);
      for (const d of manifest.diagnostics) {
        const start = this.lineMap.offsetOf(d.line, 0);
        const end = this.lineMap.offsetOf(d.line + 1, 0);
        this.diagnostics.push({ span: { start, end: Math.max(start + 1, end - 1) }, message: d.message, severity: 'error' });
      }
      return;
    }

    this.declareTopLevel();
    this.walkTopLevel();
    this.reportUnused();
    this.references.sort((a, b) => a.span.start - b.span.start);
  }

  // ------------------------------------------------------------------ scopes

  private newScope(kind: ScopeKind, span: Span, parent?: Scope, owner?: NSymbol): Scope {
    const scope: Scope = { kind, span, parent, symbols: new Map(), owner };
    this.scopes.push(scope);
    return scope;
  }

  private declare(scope: Scope, sym: NSymbol, reportDuplicates = true): void {
    const list = scope.symbols.get(sym.name);
    if (list) {
      if (reportDuplicates && sym.name) {
        const clash = list.find(other => this.conflicts(other, sym));
        if (clash) {
          // a local redeclared in the same block is an assignment for novusc
          const localRedeclaration = scope.kind !== 'module' && scope.kind !== 'class' && clash.kind === sym.kind;
          this.diagnostics.push({
            span: sym.selectionSpan,
            message: localRedeclaration ? `'${sym.name}' is already declared in this scope (novusc treats this as an assignment)` : this.duplicateMessage(sym, clash),
            severity: sym.kind === 'method' || sym.kind === 'constructor' || CLASS_LIKE.has(sym.kind) || localRedeclaration ? 'warning' : 'error',
          });
        }
      }
      list.push(sym);
    } else {
      scope.symbols.set(sym.name, [sym]);
    }
    this.allSymbols.push(sym);
  }

  private conflicts(a: NSymbol, b: NSymbol): boolean {
    const callable = (s: NSymbol) => s.kind === 'method' || s.kind === 'constructor';
    if (callable(a) && callable(b)) {
      const pa = a.params ?? [];
      const pb = b.params ?? [];
      if (pa.length !== pb.length) return false;
      return pa.every((p, i) => sameType(p.type, pb[i].type));
    }
    if (callable(a) !== callable(b)) return false;
    return true;
  }

  private duplicateMessage(sym: NSymbol, clash: NSymbol): string {
    if (sym.kind === 'method' || sym.kind === 'constructor') {
      return `Duplicate ${sym.kind} '${sym.name}' with the same parameter types`;
    }
    if (CLASS_LIKE.has(sym.kind)) return `Duplicate definition of '${sym.name}' (already defined as ${clash.kind})`;
    return `'${sym.name}' is already declared in this scope`;
  }

  scopeAt(offset: number): Scope {
    let best = this.moduleScope;
    for (const s of this.scopes) {
      if (s.span.start <= offset && offset <= s.span.end) {
        if (s.span.end - s.span.start <= best.span.end - best.span.start) best = s;
      }
    }
    return best;
  }

  enclosingClass(scope: Scope | undefined): NSymbol | undefined {
    for (let s = scope; s; s = s.parent) {
      if (s.kind === 'class' && s.owner) return s.owner;
    }
    return undefined;
  }

  enclosingMethod(scope: Scope | undefined): NSymbol | undefined {
    for (let s = scope; s; s = s.parent) {
      if (s.kind === 'method' && s.owner) return s.owner;
    }
    return undefined;
  }

  // ------------------------------------------------------------------ lookup

  /**
   * Resolves `name` from `scope` outwards, then in other files and builtin modules.
   * Symbols from packages that are not imported are returned as a last resort so that
   * navigation keeps working; callers report them via `isVisible`.
   */
  lookup(name: string, scope: Scope | undefined): NSymbol[] {
    for (let s = scope; s; s = s.parent) {
      const found = s.symbols.get(name);
      if (found && found.length) return found;
      if (s.kind === 'class' && s.owner) {
        const inherited = this.membersOf(s.owner).filter(m => m.name === name);
        if (inherited.length) return inherited;
      }
    }
    return this.globalCandidates(name);
  }

  /** Global candidates for `name`: visible ones first; otherwise importable ones. */
  private globalCandidates(name: string): NSymbol[] {
    const all = [...this.opts.globalLookup(name, this.uri)];
    const builtin = this.opts.builtinModule(name);
    if (builtin) all.push(builtin);
    if (all.length === 0) {
      for (const g of builtinGlobals()) {
        if (g.name === name) all.push(g);
      }
    }
    const visible = all.filter(s => this.isVisible(s));
    return visible.length ? visible : all;
  }

  /** Resolves a type name (class-like symbols and modules). */
  lookupType(name: string, scope?: Scope): NSymbol | undefined {
    const short = name.includes('.') ? name.slice(name.lastIndexOf('.') + 1) : name;
    for (let s = scope; s; s = s.parent) {
      const found = (s.symbols.get(short) ?? []).find(x => TYPE_LIKE.has(x.kind));
      if (found) return found;
    }
    const local = (this.moduleScope.symbols.get(short) ?? []).find(x => TYPE_LIKE.has(x.kind));
    if (local) return local;
    return this.globalCandidates(short).find(x => TYPE_LIKE.has(x.kind));
  }

  // ------------------------------------------------------------ packages

  /** Whether `sym` can be used in this file without adding an import. */
  isVisible(sym: NSymbol): boolean {
    if (sym.uri === this.uri) return true;
    if (sym.builtin) return sym.kind !== 'module' || this.importNames.includes(sym.name);
    const pkg = this.opts.packageOf(sym.uri);
    if (!pkg) return true; // files without a package are visible everywhere
    return pkg === (this.packageName ?? '') || this.importNames.includes(pkg);
  }

  /** Package (or builtin module name) that has to be imported to use `sym`. */
  packageFor(sym: NSymbol): string | undefined {
    if (sym.builtin) return sym.kind === 'module' ? sym.name : undefined;
    if (sym.uri === this.uri) return undefined;
    return this.opts.packageOf(sym.uri) || undefined;
  }

  /** Packages that declare a symbol named `name` and are not imported yet. */
  importablePackages(name: string, filter: (s: NSymbol) => boolean = () => true): string[] {
    const pkgs = new Set<string>();
    for (const s of this.opts.globalLookup(name, this.uri)) {
      if (this.isVisible(s) || !filter(s)) continue;
      const pkg = this.packageFor(s);
      if (pkg) pkgs.add(pkg);
    }
    const builtin = this.opts.builtinModule(name);
    if (builtin && !this.isVisible(builtin) && filter(builtin)) pkgs.add(builtin.name);
    return [...pkgs].sort();
  }

  /** All missing imports of this file (deduplicated, sorted). */
  missingImports(): string[] {
    const pkgs = new Set<string>();
    for (const d of this.diagnostics) {
      if (d.code !== 'missing-import') continue;
      const data = d.data as MissingImportData;
      if (data.packages.length) pkgs.add(data.packages[0]);
    }
    return [...pkgs].sort();
  }

  private reportMissingImport(span: Span, sym: NSymbol, filter?: (s: NSymbol) => boolean): void {
    const packages = this.importablePackages(sym.name, filter);
    const pkg = this.packageFor(sym);
    if (pkg && !packages.includes(pkg)) packages.unshift(pkg);
    if (!packages.length) return;
    const message =
      sym.builtin && sym.kind === 'module'
        ? `Module '${sym.name}' is not imported – add 'import ${sym.name}'`
        : `'${sym.name}' is defined in package '${packages[0]}' but not imported`;
    const data: MissingImportData = { name: sym.name, packages };
    this.diagnostics.push({ span, message, severity: 'warning', code: 'missing-import', data });
  }

  /** All members of a class-like symbol including synthesized accessors and inherited members. */
  membersOf(sym: NSymbol, seen: Set<string> = new Set()): NSymbol[] {
    if (seen.has(sym.id)) return [];
    seen.add(sym.id);
    const out: NSymbol[] = [...sym.members];
    if (sym.kind === 'enum') out.push(...enumBuiltinMembers(sym));
    for (const base of sym.bases) {
      const baseSym = this.lookupType(base.name);
      if (baseSym && CLASS_LIKE.has(baseSym.kind)) {
        for (const m of this.membersOf(baseSym, seen)) {
          if (m.kind === 'constructor') continue;
          out.push(m);
        }
      }
    }
    return out;
  }

  /** Members available on a value of the given type. */
  membersOfType(type: ast.TypeRef | undefined, scope?: Scope): NSymbol[] {
    if (!type || !type.name) return [];
    const sym = this.lookupType(type.name, scope);
    if (sym) return this.membersOf(sym);
    return builtinMembers(type.name, type.args[0] ? typeToString(type.args[0]) : undefined);
  }

  /** Symbols visible at `offset` (innermost first, shadowed names removed). */
  visibleSymbols(offset: number, globals: NSymbol[]): NSymbol[] {
    const out: NSymbol[] = [];
    const seenNames = new Set<string>();
    const seenIds = new Set<string>();
    const add = (sym: NSymbol): void => {
      if (seenIds.has(sym.id)) return;
      if (seenNames.has(sym.name) && sym.kind !== 'method' && sym.kind !== 'constructor') return;
      seenIds.add(sym.id);
      out.push(sym);
    };
    const finishLevel = (syms: NSymbol[]): void => {
      for (const s of syms) seenNames.add(s.name);
    };
    for (let s: Scope | undefined = this.scopeAt(offset); s; s = s.parent) {
      const level: NSymbol[] = [];
      for (const list of s.symbols.values()) for (const sym of list) level.push(sym);
      if (s.kind === 'class' && s.owner) level.push(...this.membersOf(s.owner));
      level.forEach(add);
      finishLevel(level);
    }
    globals.forEach(add);
    return out;
  }

  // ------------------------------------------------------------ references

  referenceAt(offset: number): Reference | undefined {
    let best: Reference | undefined;
    for (const r of this.references) {
      if (r.span.start <= offset && offset <= r.span.end) {
        if (!best || r.span.end - r.span.start < best.span.end - best.span.start) best = r;
      }
      if (r.span.start > offset) break;
    }
    return best;
  }

  symbolById(id: string): NSymbol | undefined {
    return this.allSymbols.find(s => s.id === id);
  }

  private addRef(span: Span, symbol: NSymbol | undefined, tokenType: string, declaration = false, extraModifiers: string[] = []): void {
    if (span.end <= span.start) return;
    const modifiers = symbol ? semanticTokenModifiers(symbol) : [];
    if (declaration) modifiers.push('declaration');
    modifiers.push(...extraModifiers);
    this.references.push({ span, symbol, tokenType, modifiers, declaration });
    if (symbol && !declaration) symbol.referenced = true;
  }

  private refSymbol(span: Span, symbol: NSymbol, declaration: boolean): void {
    this.addRef(span, symbol, semanticTokenType(symbol), declaration);
  }

  // ----------------------------------------------------------- declarations

  private declareTopLevel(): void {
    for (const item of this.program.items) {
      switch (item.kind) {
        case 'Package': {
          this.packageName = item.name;
          const sym = newSymbol({ kind: 'package', name: item.name, uri: this.uri, span: item, selectionSpan: item.nameSpan, decl: item });
          this.declSymbols.set(item, sym);
          this.symbols.push(sym);
          break;
        }
        case 'Import': {
          if (item.isFile) {
            // `import "file.nv"`: the imported file's package becomes visible
            // (novusc flattens file imports; the extension maps them to packages).
            const pkg = this.opts.packageOf(this.resolveFileImport(item.path ?? ''));
            if (pkg) this.importNames.push(pkg);
            this.importNames.push(item.name);
            break;
          }
          const short = item.name.includes('.') ? item.name.slice(item.name.lastIndexOf('.') + 1) : item.name;
          const builtin = this.opts.builtinModule(item.name) ?? this.opts.builtinModule(short);
          this.importNames.push(item.name);
          if (short !== item.name) this.importNames.push(short);
          const sym = newSymbol({
            kind: 'module',
            name: short,
            uri: this.uri,
            span: item,
            selectionSpan: item.nameSpan,
            decl: item,
            members: builtin ? builtin.members : [],
            doc: builtin?.doc ?? `Module \`${item.name}\``,
          });
          this.declSymbols.set(item, sym);
          this.declare(this.moduleScope, sym);
          this.imports.push(sym);
          this.symbols.push(sym);
          break;
        }
        case 'Method':
          this.symbols.push(this.declareMethod(item, this.moduleScope, undefined));
          break;
        case 'Field':
          this.symbols.push(this.declareField(item, this.moduleScope, undefined));
          break;
        case 'Define':
          this.symbols.push(this.declareDefine(item, this.moduleScope, undefined));
          break;
        default:
          break;
      }
    }
  }

  private modifiersOf(decl: ast.DeclBase): string[] {
    return decl.modifiers.map(m => m.name);
  }

  private deprecationOf(decl: ast.DeclBase): string | undefined {
    const ann = decl.annotations.find(a => a.name === 'Deprecated');
    if (!ann) return undefined;
    const text = ann.args.find(a => a.name === 'text')?.value;
    return text && text.kind === 'String' ? text.value : 'Deprecated';
  }

  private declareMethod(decl: ast.MethodDecl, scope: Scope, container: NSymbol | undefined): NSymbol {
    const sym = newSymbol({
      kind: decl.isConstructor ? 'constructor' : 'method',
      name: decl.name,
      uri: this.uri,
      span: decl,
      selectionSpan: decl.nameSpan,
      params: decl.params,
      returnType: decl.returnType,
      modifiers: this.modifiersOf(decl),
      annotations: decl.annotations.map(a => a.name),
      doc: decl.doc,
      container,
      deprecated: this.deprecationOf(decl),
      decl,
    });
    this.declSymbols.set(decl, sym);
    if (decl.name || decl.isConstructor) this.declare(scope, sym);
    else this.allSymbols.push(sym);

    const methodScope = this.newScope('method', decl, scope, sym);
    this.declScopes.set(decl, methodScope);
    for (const p of decl.params) {
      if (!p.name) continue;
      const ps = newSymbol({ kind: 'parameter', name: p.name, uri: this.uri, span: p, selectionSpan: p.nameSpan, type: p.type, container: sym, decl: p });
      this.declSymbols.set(p, ps);
      this.declare(methodScope, ps);
    }
    return sym;
  }

  private declareField(decl: ast.FieldDecl, scope: Scope, container: NSymbol | undefined): NSymbol {
    const modifiers = this.modifiersOf(decl);
    let kind: NSymbol['kind'];
    if (container) kind = 'field';
    else if (decl.isVar) kind = 'variable';
    else kind = modifiers.includes('final') ? 'constant' : 'variable';
    const sym = newSymbol({
      kind,
      name: decl.name,
      uri: this.uri,
      span: decl,
      selectionSpan: decl.nameSpan,
      type: decl.type,
      modifiers,
      annotations: decl.annotations.map(a => a.name),
      accessors: decl.accessors.map(a => a.name),
      doc: decl.doc,
      container,
      deprecated: this.deprecationOf(decl),
      decl,
    });
    this.declSymbols.set(decl, sym);
    if (decl.name) this.declare(scope, sym);
    else this.allSymbols.push(sym);
    return sym;
  }

  private declareDefine(decl: ast.DefineDecl, scope: Scope, container: NSymbol | undefined): NSymbol {
    const sym = newSymbol({
      kind: decl.defineKind,
      name: decl.name,
      uri: this.uri,
      span: decl,
      selectionSpan: decl.nameSpan,
      modifiers: this.modifiersOf(decl),
      annotations: decl.annotations.map(a => a.name),
      bases: decl.bases,
      doc: decl.doc,
      container,
      deprecated: this.deprecationOf(decl),
      decl,
    });
    this.declSymbols.set(decl, sym);
    if (decl.name) this.declare(scope, sym);
    else this.allSymbols.push(sym);

    const classScope = this.newScope('class', decl, scope, sym);
    this.declScopes.set(decl, classScope);

    if (decl.params.length) {
      const ctor = newSymbol({
        kind: 'constructor',
        name: 'construct',
        uri: this.uri,
        span: decl,
        selectionSpan: decl.nameSpan,
        params: decl.params,
        container: sym,
        synthesized: true,
        doc: `Primary constructor of ${decl.name}`,
      });
      sym.members.push(ctor);
      this.allSymbols.push(ctor);
      const ctorScope = this.newScope('method', { start: decl.params[0].start, end: decl.params[decl.params.length - 1].end }, classScope, ctor);
      for (const p of decl.params) {
        if (!p.name) continue;
        const ps = newSymbol({ kind: 'parameter', name: p.name, uri: this.uri, span: p, selectionSpan: p.nameSpan, type: p.type, container: ctor, decl: p });
        this.declSymbols.set(p, ps);
        this.declare(ctorScope, ps);
      }
    }

    for (const c of decl.constants) {
      const cs = newSymbol({
        kind: 'enumMember',
        name: c.name,
        uri: this.uri,
        span: c,
        selectionSpan: c.nameSpan,
        type: mkType(decl.name),
        container: sym,
        decl: c,
      });
      this.declSymbols.set(c, cs);
      this.declare(classScope, cs);
      sym.members.push(cs);
    }

    for (const m of decl.members) {
      let ms: NSymbol;
      if (m.kind === 'Method') ms = this.declareMethod(m, classScope, sym);
      else if (m.kind === 'Field') ms = this.declareField(m, classScope, sym);
      else ms = this.declareDefine(m, classScope, sym);
      sym.members.push(ms);
      if (m.kind === 'Field') {
        for (const acc of m.accessors) {
          const getter = acc.name === 'get';
          const accessor = newSymbol({
            id: ms.id,
            kind: 'method',
            name: m.name,
            uri: this.uri,
            span: m,
            selectionSpan: m.nameSpan,
            params: getter ? [] : [{ kind: 'Param', type: m.type, name: 'value', nameSpan: m.nameSpan, start: m.start, end: m.end }],
            returnType: getter ? m.type : undefined,
            container: sym,
            accessorOf: ms,
            synthesized: true,
            doc: `${getter ? 'Getter' : 'Setter'} for field \`${m.name}\`${m.doc ? '\n\n' + m.doc : ''}`,
          });
          sym.members.push(accessor);
        }
      }
    }
    return sym;
  }

  /** URI of a file import target: a project dependency module, else relative to this document. */
  private resolveFileImport(rel: string): string {
    try {
      const base = URI.parse(this.uri).fsPath;
      const local = path.resolve(path.dirname(base), rel);
      if (!fs.existsSync(local) && this.opts.resolveModuleImport) {
        const module = this.opts.resolveModuleImport(this.uri, rel);
        if (module) return module;
      }
      return URI.file(local).toString();
    } catch {
      return '';
    }
  }

  // ----------------------------------------------------------------- walking

  private walkTopLevel(): void {
    for (const item of this.program.items) {
      switch (item.kind) {
        case 'Package': {
          const sym = this.declSymbols.get(item);
          if (sym) this.refSymbol(item.nameSpan, sym, true);
          break;
        }
        case 'Import': {
          const sym = this.declSymbols.get(item);
          if (sym) this.refSymbol(item.nameSpan, sym, true);
          break;
        }
        default:
          this.walkStmt(item, this.moduleScope);
      }
    }
  }

  private walkAnnotations(annotations: ast.Annotation[], scope: Scope): void {
    for (const ann of annotations) {
      const sym = this.lookupType(ann.name, scope);
      const annSym = sym && sym.kind === 'annotation' ? sym : undefined;
      const builtin = BUILTIN_ANNOTATIONS.find(b => b.name === ann.name);
      if (annSym) {
        this.addRef(ann.nameSpan, annSym, 'decorator');
        if (!this.isVisible(annSym)) this.reportMissingImport(ann.nameSpan, annSym, s => s.kind === 'annotation');
      } else {
        this.addRef(ann.nameSpan, undefined, 'decorator', false, builtin ? ['defaultLibrary'] : []);
        if (!builtin && ann.name && this.opts.undefinedSymbols) {
          this.diagnostics.push({ span: ann.nameSpan, message: `Cannot find annotation '${ann.name}'`, severity: 'warning' });
        }
      }
      for (const arg of ann.args) {
        if (arg.name) {
          const member = annSym ? this.membersOf(annSym).find(m => m.name === arg.name) : undefined;
          this.addRef(arg.nameSpan, member, 'property');
          if (!member && this.opts.undefinedSymbols) {
            const known = annSym ? this.membersOf(annSym).map(m => m.name) : builtin?.args.map(a => a.name);
            if (known && !known.includes(arg.name)) {
              this.diagnostics.push({ span: arg.nameSpan, message: `Annotation '${ann.name}' has no argument '${arg.name}'`, severity: 'warning' });
            }
          }
        }
        if (arg.value) this.walkExpr(arg.value, scope);
      }
    }
  }

  private walkType(type: ast.TypeRef | undefined, scope: Scope): void {
    if (!type || !type.name) return;
    if (isPrimitive(type.name)) {
      this.addRef(type.nameSpan, undefined, 'type', false, ['defaultLibrary']);
    } else {
      const sym = this.lookupType(type.name, scope);
      if (sym && CLASS_LIKE.has(sym.kind)) {
        this.refSymbol(type.nameSpan, sym, false);
        if (!this.isVisible(sym)) this.reportMissingImport(type.nameSpan, sym, s => CLASS_LIKE.has(s.kind));
      } else {
        this.addRef(type.nameSpan, undefined, 'type');
      }
    }
    for (const arg of type.args) this.walkType(arg, scope);
  }

  private walkMethod(decl: ast.MethodDecl, scope: Scope): void {
    const sym = this.declSymbols.get(decl);
    const methodScope = this.declScopes.get(decl) ?? this.newScope('method', decl, scope, sym);
    this.walkAnnotations(decl.annotations, scope);
    if (sym && decl.nameSpan.end > decl.nameSpan.start) this.refSymbol(decl.nameSpan, sym, true);
    for (const p of decl.params) {
      this.walkType(p.type, scope);
      const ps = this.declSymbols.get(p);
      if (ps) this.refSymbol(p.nameSpan, ps, true);
    }
    this.walkType(decl.returnType, scope);
    if (decl.body) {
      for (const stmt of decl.body.statements) this.walkStmt(stmt, methodScope);
    } else if (sym && scope.kind !== 'class' && !decl.isNative) {
      this.diagnostics.push({ span: decl.nameSpan, message: `Method '${decl.name}' has no body`, severity: 'error' });
    } else if (sym && scope.kind === 'class' && scope.owner && scope.owner.kind === 'class' && !decl.modifiers.some(m => m.name === 'abstract')) {
      this.diagnostics.push({ span: decl.nameSpan, message: `Method '${decl.name}' has no body – add '{ }' or mark it 'abstract'`, severity: 'error' });
    }
  }

  private walkField(decl: ast.FieldDecl, scope: Scope): void {
    this.walkAnnotations(decl.annotations, scope);
    this.walkType(decl.type, scope);
    if (decl.value) this.walkExpr(decl.value, scope);
    let sym = this.declSymbols.get(decl);
    if (!sym) {
      // local variable – declare now so that the initializer cannot see it
      sym = this.declareField(decl, scope, undefined);
      sym.local = true;
      if (!decl.isVar && !decl.type) {
        // `name = value` without var inside a body was parsed as a field: treat as assignment error
      }
    }
    if (!sym.type && decl.value) sym.type = this.typeOf(decl.value, scope);
    if (decl.nameSpan.end > decl.nameSpan.start) this.refSymbol(decl.nameSpan, sym, true);
    for (const acc of decl.accessors) this.addRef(acc, undefined, 'keyword');
  }

  private walkDefine(decl: ast.DefineDecl, scope: Scope): void {
    const sym = this.declSymbols.get(decl) ?? this.declareDefine(decl, scope, undefined);
    const classScope = this.declScopes.get(decl) ?? this.newScope('class', decl, scope, sym);
    this.walkAnnotations(decl.annotations, scope);
    if (decl.nameSpan.end > decl.nameSpan.start) this.refSymbol(decl.nameSpan, sym, true);
    for (const p of decl.params) {
      this.walkType(p.type, scope);
      const ps = this.declSymbols.get(p);
      if (ps) this.refSymbol(p.nameSpan, ps, true);
    }
    for (const base of decl.bases) {
      const baseSym = this.lookupType(base.name, scope);
      if (baseSym && CLASS_LIKE.has(baseSym.kind)) {
        this.refSymbol(base.nameSpan, baseSym, false);
        if (!this.isVisible(baseSym)) this.reportMissingImport(base.nameSpan, baseSym, s => CLASS_LIKE.has(s.kind));
        if (baseSym.id === sym.id) {
          this.diagnostics.push({ span: base.nameSpan, message: `'${decl.name}' cannot be based on itself`, severity: 'error' });
        }
      } else {
        this.addRef(base.nameSpan, undefined, 'type');
        if (this.opts.undefinedSymbols && base.name) {
          this.diagnostics.push({ span: base.nameSpan, message: `Cannot find type '${base.name}'`, severity: 'warning' });
        }
      }
      for (const arg of base.args) this.walkType(arg, scope);
    }
    for (const c of decl.constants) {
      const cs = this.declSymbols.get(c);
      if (cs) this.refSymbol(c.nameSpan, cs, true);
      for (const arg of c.args) this.walkExpr(arg, classScope);
    }
    for (const m of decl.members) this.walkStmt(m, classScope);
  }

  private walkStmt(stmt: ast.Stmt | undefined, scope: Scope): void {
    if (!stmt) return;
    switch (stmt.kind) {
      case 'Method':
        this.walkMethod(stmt, scope);
        break;
      case 'Field':
        this.walkField(stmt, scope);
        break;
      case 'Define':
        this.walkDefine(stmt, scope);
        break;
      case 'Block': {
        const inner = this.newScope('block', stmt, scope, scope.owner);
        for (const s of stmt.statements) this.walkStmt(s, inner);
        break;
      }
      case 'Print':
      case 'Return':
        if (stmt.value) this.walkExpr(stmt.value, scope);
        break;
      case 'If':
        if (stmt.cond) this.walkExpr(stmt.cond, scope);
        this.walkStmt(stmt.then, scope);
        this.walkStmt(stmt.else, scope);
        break;
      case 'While':
        if (stmt.cond) this.walkExpr(stmt.cond, scope);
        this.walkStmt(stmt.body, scope);
        break;
      case 'For': {
        const loop = this.newScope('block', stmt, scope, scope.owner);
        if (stmt.iterable) this.walkExpr(stmt.iterable, scope);
        if (stmt.varName && stmt.varSpan) {
          const iterType = stmt.iterable ? this.typeOf(stmt.iterable, scope) : undefined;
          const elemType = iterType && iterType.name === 'array' ? iterType.args[0] : isStringType(iterType?.name ?? '') ? mkType('string') : undefined;
          const v = newSymbol({ kind: 'variable', name: stmt.varName, uri: this.uri, span: stmt.varSpan, selectionSpan: stmt.varSpan, type: elemType, local: true, decl: stmt });
          this.declare(loop, v);
          this.refSymbol(stmt.varSpan, v, true);
        }
        this.walkStmt(stmt.init, loop);
        if (stmt.cond) this.walkExpr(stmt.cond, loop);
        this.walkStmt(stmt.update, loop);
        if (stmt.body && stmt.body.kind === 'Block') {
          for (const s of stmt.body.statements) this.walkStmt(s, loop);
        } else {
          this.walkStmt(stmt.body, loop);
        }
        break;
      }
      case 'ExprStmt':
        this.walkExpr(stmt.expr, scope);
        break;
      case 'Assign': {
        this.walkExpr(stmt.target, scope, { assign: true });
        if (stmt.value) this.walkExpr(stmt.value, scope);
        if (stmt.target.kind === 'Ident') {
          const sym = this.lookup(stmt.target.name, scope)[0];
          if (sym && (sym.kind === 'constant' || (sym.kind === 'variable' && sym.modifiers.includes('final')))) {
            this.diagnostics.push({ span: stmt.target, message: `Cannot assign to '${sym.name}' because it is final`, severity: 'error' });
          }
          if (sym && !sym.type && sym.local) sym.type = this.typeOf(stmt.value, scope);
        }
        break;
      }
      case 'Break':
      case 'Continue':
        break;
    }
  }

  private walkExpr(expr: ast.Expr | undefined, scope: Scope, ctx: { callee?: boolean; assign?: boolean } = {}): void {
    if (!expr) return;
    switch (expr.kind) {
      case 'Ident': {
        const syms = this.lookup(expr.name, scope);
        let sym = syms[0];
        if (ctx.callee) sym = syms.find(s => s.kind === 'method' || s.kind === 'constructor' || CLASS_LIKE.has(s.kind)) ?? sym;
        if (sym) {
          this.refSymbol(expr, sym, false);
          if (!this.isVisible(sym)) this.reportMissingImport(expr, sym);
          if (sym.deprecated !== undefined) {
            this.diagnostics.push({ span: expr, message: `'${sym.name}' is deprecated: ${sym.deprecated}`, severity: 'hint', tags: ['deprecated'] });
          }
        } else {
          this.addRef(expr, undefined, ctx.callee ? 'function' : 'variable');
          if (this.opts.undefinedSymbols && expr.name) {
            this.diagnostics.push({ span: expr, message: `Cannot find name '${expr.name}'`, severity: 'warning' });
          }
        }
        break;
      }
      case 'This': {
        const cls = this.enclosingClass(scope);
        if (!cls) this.diagnostics.push({ span: expr, message: "'this' can only be used inside a class", severity: 'error' });
        break;
      }
      case 'Member': {
        this.walkExpr(expr.object, scope);
        const objType = this.typeOf(expr.object, scope);
        const members = this.membersOfType(objType, scope);
        const candidates = members.filter(m => m.name === expr.name);
        let member: NSymbol | undefined;
        if (ctx.callee) member = candidates.find(m => m.kind === 'method' || m.kind === 'constructor') ?? candidates[0];
        else member = candidates.find(m => m.kind !== 'method' && m.kind !== 'constructor') ?? candidates[0];
        if (member) {
          this.refSymbol(expr.nameSpan, member, false);
          if (member.deprecated !== undefined) {
            this.diagnostics.push({ span: expr.nameSpan, message: `'${member.name}' is deprecated: ${member.deprecated}`, severity: 'hint', tags: ['deprecated'] });
          }
        } else {
          this.addRef(expr.nameSpan, undefined, ctx.callee ? 'method' : 'property');
          if (this.opts.undefinedSymbols && expr.name && objType && members.length) {
            const owner = this.lookupType(objType.name, scope);
            if (owner && owner.kind !== 'module') {
              this.diagnostics.push({ span: expr.nameSpan, message: `'${expr.name}' does not exist on type '${typeToString(objType)}'`, severity: 'warning' });
            }
          }
        }
        break;
      }
      case 'Call': {
        this.walkExpr(expr.callee, scope, { callee: true });
        for (const a of expr.args) this.walkExpr(a, scope);
        break;
      }
      case 'Index':
        this.walkExpr(expr.object, scope);
        this.walkExpr(expr.index, scope);
        break;
      case 'Binary':
        this.walkExpr(expr.left, scope);
        this.walkExpr(expr.right, scope);
        break;
      case 'Unary':
        this.walkExpr(expr.operand, scope);
        break;
      case 'Paren':
        this.walkExpr(expr.expr, scope);
        break;
      case 'String':
        for (const e of expr.interpolations) this.walkExpr(e, scope);
        break;
      case 'Array':
        for (const e of expr.elements) this.walkExpr(e, scope);
        break;
      case 'Map':
        for (const e of expr.entries) {
          this.walkExpr(e.key, scope);
          this.walkExpr(e.value, scope);
        }
        break;
      case 'StructInit': {
        let cls: NSymbol | undefined;
        if (expr.target.kind === 'Ident') {
          cls = this.lookupType(expr.target.name, scope);
          if (cls && CLASS_LIKE.has(cls.kind)) {
            this.refSymbol(expr.target, cls, false);
            if (!this.isVisible(cls)) this.reportMissingImport(expr.target, cls, s => CLASS_LIKE.has(s.kind));
          } else {
            this.addRef(expr.target, undefined, 'type');
            if (this.opts.undefinedSymbols) {
              this.diagnostics.push({ span: expr.target, message: `Cannot find type '${expr.target.name}'`, severity: 'warning' });
            }
            cls = undefined;
          }
        } else {
          this.walkExpr(expr.target, scope);
        }
        const fields = cls ? this.membersOf(cls).filter(m => m.kind === 'field') : [];
        for (const arg of expr.args) {
          if (arg.name) {
            const f = fields.find(x => x.name === arg.name);
            this.addRef(arg.nameSpan, f, 'property');
            if (cls && !f && this.opts.undefinedSymbols) {
              this.diagnostics.push({ span: arg.nameSpan, message: `'${cls.name}' has no field '${arg.name}'`, severity: 'warning' });
            }
          }
          this.walkExpr(arg.value, scope);
        }
        break;
      }
      case 'Int':
      case 'Float':
      case 'Bool':
      case 'Null':
        break;
    }
  }

  // ---------------------------------------------------------------- typing

  /** Type of a declared symbol, inferring from the initializer when needed. */
  typeOfSymbol(sym: NSymbol): ast.TypeRef | undefined {
    if (sym.type) return sym.type;
    if (sym.kind === 'enumMember' && sym.container) return mkType(sym.container.name);
    if (CLASS_LIKE.has(sym.kind) || sym.kind === 'module') return mkType(sym.name);
    if (this.inferring.has(sym)) return undefined;
    const decl = sym.decl;
    if (decl && decl.kind === 'Field' && decl.value && sym.uri === this.uri) {
      this.inferring.add(sym);
      try {
        const scope = this.scopeAt(decl.start);
        sym.type = this.typeOf(decl.value, scope);
      } finally {
        this.inferring.delete(sym);
      }
    }
    return sym.type;
  }

  typeOf(expr: ast.Expr | undefined, scope: Scope): ast.TypeRef | undefined {
    if (!expr) return undefined;
    switch (expr.kind) {
      case 'Int':
        return mkType('integer');
      case 'Float':
        return mkType('float');
      case 'String':
        return mkType('string');
      case 'Bool':
        return mkType('bool');
      case 'Null':
        return undefined;
      case 'Array': {
        const elem = expr.elements.length ? this.typeOf(expr.elements[0], scope) : undefined;
        return mkType('array', elem ? [elem] : []);
      }
      case 'Map':
        return mkType('map');
      case 'Paren':
        return this.typeOf(expr.expr, scope);
      case 'This': {
        const cls = this.enclosingClass(scope);
        return cls ? mkType(cls.name) : undefined;
      }
      case 'Ident': {
        const sym = this.lookup(expr.name, scope)[0];
        return sym ? this.typeOfSymbol(sym) : undefined;
      }
      case 'StructInit':
        return expr.target.kind === 'Ident' ? mkType(expr.target.name) : this.typeOf(expr.target, scope);
      case 'Member': {
        const objType = this.typeOf(expr.object, scope);
        const member = this.membersOfType(objType, scope).find(m => m.name === expr.name && m.kind !== 'method' && m.kind !== 'constructor');
        return member ? this.typeOfSymbol(member) : undefined;
      }
      case 'Call': {
        const callee = expr.callee;
        if (callee.kind === 'Ident') {
          const syms = this.lookup(callee.name, scope);
          const cls = syms.find(s => CLASS_LIKE.has(s.kind));
          if (cls) return mkType(cls.name);
          const method = this.pickOverload(syms.filter(s => s.kind === 'method'), expr.args.length);
          return method?.returnType;
        }
        if (callee.kind === 'Member') {
          const objType = this.typeOf(callee.object, scope);
          const methods = this.membersOfType(objType, scope).filter(m => m.name === callee.name && m.kind === 'method');
          const method = this.pickOverload(methods, expr.args.length);
          return method?.returnType;
        }
        return undefined;
      }
      case 'Index': {
        const objType = this.typeOf(expr.object, scope);
        if (objType?.name === 'array') return objType.args[0];
        if (objType && isStringType(objType.name)) return mkType('string');
        return undefined;
      }
      case 'Unary':
        return expr.op === '!' ? mkType('bool') : this.typeOf(expr.operand, scope);
      case 'Binary': {
        if (['==', '!=', '<', '>', '<=', '>=', '&&', '||'].includes(expr.op)) return mkType('bool');
        const l = this.typeOf(expr.left, scope);
        const r = this.typeOf(expr.right, scope);
        if (expr.op === '+' && ((l && isStringType(l.name)) || (r && isStringType(r.name)))) return mkType('string');
        if ((l && isFloatType(l.name)) || (r && isFloatType(r.name))) return mkType('float');
        if (l && r && isBoolType(l.name) && isBoolType(r.name)) return mkType('bool');
        if (l || r) return mkType('integer');
        return undefined;
      }
    }
  }

  pickOverload(methods: NSymbol[], argCount: number): NSymbol | undefined {
    return methods.find(m => (m.params ?? []).length === argCount) ?? methods[0];
  }

  // ------------------------------------------------------------ diagnostics

  private reportUnused(): void {
    if (!this.opts.unusedVariables) return;
    for (const sym of this.allSymbols) {
      if (sym.kind !== 'variable' || !sym.local || sym.referenced || !sym.name) continue;
      this.diagnostics.push({
        span: sym.selectionSpan,
        message: `'${sym.name}' is declared but never used`,
        severity: 'hint',
        tags: ['unnecessary'],
      });
    }
  }
}
