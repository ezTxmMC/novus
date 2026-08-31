/**
 * Recursive-descent parser for Novus with error recovery.
 *
 * Accepts a superset of what the C++ parser in src/parser currently handles so
 * that the concept syntax from test/syntax.nv is fully understood.
 */
import * as ast from './ast';
import { Comment, DEFINE_KINDS, MODIFIER_WORDS, RESERVED_WORDS, Span, Token, TokenKind, lex } from './lexer';

export interface ParseError extends Span {
  message: string;
}

export interface ParseResult {
  program: ast.Program;
  errors: ParseError[];
  comments: Comment[];
  lineMap: LineMap;
}

/** Maps between offsets and line/character positions. */
export class LineMap {
  private readonly starts: number[] = [0];

  constructor(public readonly text: string) {
    for (let i = 0; i < text.length; i++) {
      if (text.charCodeAt(i) === 10) this.starts.push(i + 1);
    }
  }

  get lineCount(): number {
    return this.starts.length;
  }

  lineOf(offset: number): number {
    let lo = 0;
    let hi = this.starts.length - 1;
    while (lo < hi) {
      const mid = (lo + hi + 1) >> 1;
      if (this.starts[mid] <= offset) lo = mid;
      else hi = mid - 1;
    }
    return lo;
  }

  lineStart(line: number): number {
    return this.starts[Math.max(0, Math.min(line, this.starts.length - 1))];
  }

  positionOf(offset: number): { line: number; character: number } {
    const clamped = Math.max(0, Math.min(offset, this.text.length));
    const line = this.lineOf(clamped);
    return { line, character: clamped - this.starts[line] };
  }

  offsetOf(line: number, character: number): number {
    if (line >= this.starts.length) return this.text.length;
    return Math.min(this.starts[line] + character, this.text.length);
  }
}

const STATEMENT_STARTERS = new Set(['var', 'println', 'return', 'if', 'for', 'while', 'method', 'define', 'construct', 'package', 'import', 'break', 'continue']);

export function parse(src: string): ParseResult {
  const lexed = lex(src);
  const lineMap = new LineMap(src);
  const parser = new Parser(src, lexed.tokens, lexed.comments, lineMap);
  const program = parser.parseProgram();
  const errors: ParseError[] = [...lexed.errors, ...parser.errors];
  errors.sort((a, b) => a.start - b.start);
  return { program, errors, comments: lexed.comments, lineMap };
}

export class Parser {
  private pos = 0;
  readonly errors: ParseError[] = [];
  private lastErrorStart = -1;

  constructor(
    private readonly src: string,
    private readonly tokens: Token[],
    private readonly comments: Comment[],
    private readonly lineMap: LineMap,
  ) {}

  // ---------------------------------------------------------------- helpers

  private get cur(): Token {
    return this.tokens[this.pos];
  }

  private peek(n = 1): Token {
    return this.tokens[Math.min(this.pos + n, this.tokens.length - 1)];
  }

  private advance(): Token {
    const t = this.cur;
    if (this.pos < this.tokens.length - 1) this.pos++;
    return t;
  }

  private atEOF(): boolean {
    return this.cur.kind === TokenKind.EOF;
  }

  private isPunct(v: string, t: Token = this.cur): boolean {
    return t.kind === TokenKind.Punct && t.value === v;
  }

  private isKw(v: string, t: Token = this.cur): boolean {
    return t.kind === TokenKind.Identifier && t.value === v;
  }

  private isIdent(t: Token = this.cur): boolean {
    return t.kind === TokenKind.Identifier && !RESERVED_WORDS.has(t.value);
  }

  private isModifier(t: Token = this.cur): boolean {
    return t.kind === TokenKind.Identifier && MODIFIER_WORDS.has(t.value);
  }

  private prevEnd(): number {
    return this.pos > 0 ? this.tokens[this.pos - 1].end : 0;
  }

  private emptySpanHere(): Span {
    const at = this.prevEnd();
    return { start: at, end: at };
  }

  private error(message: string, span: Span = this.cur): void {
    this.lastErrorStart = span.start;
    this.errors.push({ message, start: span.start, end: Math.max(span.end, span.start + 1) });
  }

  private describeToken(t: Token = this.cur): string {
    if (t.kind === TokenKind.EOF) return 'end of file';
    if (t.kind === TokenKind.String) return 'string literal';
    return `'${t.value}'`;
  }

  private expectPunct(v: string, context?: string): Token | undefined {
    if (this.isPunct(v)) return this.advance();
    this.error(`Expected '${v}'${context ? ' ' + context : ''}, found ${this.describeToken()}`);
    return undefined;
  }

  private skipSemis(): void {
    while (this.isPunct(';')) this.advance();
  }

  private isStatementStart(t: Token = this.cur): boolean {
    return t.kind === TokenKind.Identifier && STATEMENT_STARTERS.has(t.value);
  }

  /** Reports an "unexpected token" error unless one was already reported at this token. */
  private unexpected(context: string): void {
    if (this.lastErrorStart !== this.cur.start) {
      this.error(`Unexpected ${this.describeToken()} ${context}`);
    }
  }

  // ------------------------------------------------------------- doc comments

  private docFor(declStart: number): string | undefined {
    const declLine = this.lineMap.lineOf(declStart);
    let idx = -1;
    for (let i = this.comments.length - 1; i >= 0; i--) {
      if (this.comments[i].end <= declStart) {
        idx = i;
        break;
      }
    }
    if (idx < 0) return undefined;
    const collected: Comment[] = [];
    let expectedLine = declLine - 1;
    for (let i = idx; i >= 0; i--) {
      const c = this.comments[i];
      const endLine = this.lineMap.lineOf(Math.max(c.start, c.end - 1));
      if (endLine !== expectedLine) break;
      const lineStart = this.lineMap.lineStart(this.lineMap.lineOf(c.start));
      if (this.src.slice(lineStart, c.start).trim() !== '') break;
      collected.unshift(c);
      if (c.block) break;
      expectedLine = this.lineMap.lineOf(c.start) - 1;
    }
    if (!collected.length) return undefined;
    const lines: string[] = [];
    for (const c of collected) {
      if (c.block) {
        const body = c.text.replace(/^\/\*\*?/, '').replace(/\*\/$/, '');
        for (const line of body.split('\n')) lines.push(line.replace(/^\s*\*\s?/, '').trimEnd());
      } else {
        lines.push(c.text.replace(/^\/\/\/?\s?/, '').trimEnd());
      }
    }
    while (lines.length && !lines[0].trim()) lines.shift();
    while (lines.length && !lines[lines.length - 1].trim()) lines.pop();
    return lines.length ? lines.join('\n') : undefined;
  }

  // ----------------------------------------------------------------- program

  parseProgram(): ast.Program {
    const items: ast.TopLevel[] = [];
    while (!this.atEOF()) {
      this.skipSemis();
      if (this.atEOF()) break;
      const startPos = this.pos;
      const item = this.parseTopLevel();
      if (item) items.push(item);
      if (this.pos === startPos) {
        this.unexpected('at top level');
        this.advance();
      }
    }
    return { kind: 'Program', items, start: 0, end: this.src.length };
  }

  private parseTopLevel(): ast.TopLevel | undefined {
    if (this.isKw('package')) return this.parsePackage();
    if (this.isKw('import')) return this.parseImport();
    if (this.isPunct('}')) {
      this.error("Unexpected '}' – no matching '{'");
      this.advance();
      return undefined;
    }

    const declStart = this.cur.start;
    const annotations = this.parseAnnotations();
    const modifiers = this.parseModifiers();

    if (this.isKw('method')) return this.parseMethod(declStart, annotations, modifiers, true, false);
    if (this.isKw('construct')) return this.parseMethod(declStart, annotations, modifiers, true, true);
    if (this.isKw('define')) return this.parseDefine(declStart, annotations, modifiers);
    if (this.isKw('var')) return this.parseVar(declStart, annotations, modifiers);
    if (this.looksLikeTypedDecl()) return this.parseField(declStart, annotations, modifiers);

    if (annotations.length || modifiers.length) {
      if (this.isIdent() && this.isPunct('=', this.peek())) return this.parseField(declStart, annotations, modifiers);
      if (this.isIdent() && this.isPunct('(', this.peek())) return this.parseMethod(declStart, annotations, modifiers, false, false);
      this.error(`Expected a declaration after ${annotations.length ? 'annotation' : 'modifier'}, found ${this.describeToken()}`);
      return undefined;
    }

    return this.parseStatement();
  }

  private parsePackage(): ast.PackageDecl {
    const kw = this.advance();
    const { name, nameSpan } = this.parseDottedName('package name');
    return { kind: 'Package', name, nameSpan, start: kw.start, end: this.prevEnd() };
  }

  private parseImport(): ast.ImportDecl {
    const kw = this.advance();
    if (this.cur.kind === TokenKind.String && !this.cur.newlineBefore) {
      // File import: import "path/file.nv" - the imported file's symbols are
      // resolved via its package (usually named after the file).
      const tok = this.advance();
      const file = tok.value.split(/[\\/]/).pop() ?? tok.value;
      const name = file.replace(/\.nv$/, '');
      return {
        kind: 'Import',
        name,
        isFile: true,
        path: tok.value,
        nameSpan: { start: tok.start, end: tok.end },
        start: kw.start,
        end: this.prevEnd(),
      };
    }
    const { name, nameSpan } = this.parseDottedName('module name');
    return { kind: 'Import', name, nameSpan, start: kw.start, end: this.prevEnd() };
  }

  private parseDottedName(what: string): { name: string; nameSpan: Span } {
    if (!this.isIdent() || this.cur.newlineBefore) {
      this.error(`Expected ${what}`);
      return { name: '', nameSpan: this.emptySpanHere() };
    }
    const first = this.advance();
    let name = first.value;
    let end = first.end;
    while (this.isPunct('.') && this.peek().kind === TokenKind.Identifier && !this.peek().newlineBefore) {
      this.advance();
      const part = this.advance();
      name += '.' + part.value;
      end = part.end;
    }
    return { name, nameSpan: { start: first.start, end } };
  }

  // ------------------------------------------------------------ declarations

  private parseAnnotations(): ast.Annotation[] {
    const list: ast.Annotation[] = [];
    while (this.isPunct('@')) {
      const at = this.advance();
      let name = '';
      let nameSpan: Span = { start: at.end, end: at.end };
      if (this.cur.kind === TokenKind.Identifier && !this.cur.newlineBefore) {
        const t = this.advance();
        name = t.value;
        nameSpan = { start: t.start, end: t.end };
      } else {
        this.error('Expected annotation name after \'@\'');
      }
      let args: ast.NamedArg[] = [];
      if (!this.cur.newlineBefore && this.isPunct('{')) {
        this.advance();
        args = this.parseNamedArgs('}');
      } else if (!this.cur.newlineBefore && this.isPunct('(')) {
        this.advance();
        args = this.parseNamedArgs(')');
      }
      list.push({ kind: 'Annotation', name, nameSpan, args, start: at.start, end: this.prevEnd() });
    }
    return list;
  }

  private parseNamedArgs(closer: string): ast.NamedArg[] {
    const args: ast.NamedArg[] = [];
    while (!this.isPunct(closer) && !this.atEOF()) {
      const startPos = this.pos;
      if (this.isIdent() && this.isPunct('=', this.peek())) {
        const nameTok = this.advance();
        this.advance(); // '='
        const value = this.parseExpression();
        if (!value) this.error(`Expected value for '${nameTok.value}'`);
        args.push({ kind: 'NamedArg', name: nameTok.value, nameSpan: { start: nameTok.start, end: nameTok.end }, value, start: nameTok.start, end: this.prevEnd() });
      } else {
        const value = this.parseExpression();
        if (value) {
          args.push({ kind: 'NamedArg', name: '', nameSpan: { start: value.start, end: value.start }, value, start: value.start, end: value.end });
        }
      }
      if (this.isPunct(',')) {
        this.advance();
        continue;
      }
      if (!this.isPunct(closer)) {
        if (this.isStatementStart() && this.cur.newlineBefore) break;
        this.unexpected(`in argument list, expected ',' or '${closer}'`);
        if (this.pos === startPos) this.advance();
      }
    }
    this.expectPunct(closer, 'to close argument list');
    return args;
  }

  private parseModifiers(): ast.ModifierTok[] {
    const list: ast.ModifierTok[] = [];
    while (this.isModifier()) {
      // `abstract` directly followed by a define-kind name belongs to `define abstract`
      const t = this.advance();
      list.push({ name: t.value, start: t.start, end: t.end });
    }
    return list;
  }

  /** `Type name` (with optional generics) followed by `:`, `=`, `;`, `}`, a newline or EOF. */
  private looksLikeTypedDecl(): boolean {
    let i = this.pos;
    const first = this.tokens[i];
    if (!this.isIdent(first)) return false;
    i++;
    while (this.isPunct('.', this.tokens[i]) && this.tokens[i + 1]?.kind === TokenKind.Identifier) i += 2;
    if (this.isPunct('<', this.tokens[i])) {
      let depth = 0;
      while (i < this.tokens.length) {
        const t = this.tokens[i];
        if (this.isPunct('<', t)) depth++;
        else if (this.isPunct('>', t)) {
          depth--;
          if (depth === 0) {
            i++;
            break;
          }
        } else if (t.kind !== TokenKind.Identifier && !this.isPunct(',', t) && !this.isPunct('.', t)) {
          return false;
        }
        i++;
      }
      if (depth !== 0) return false;
    }
    const name = this.tokens[i];
    if (!name || !this.isIdent(name) || name.newlineBefore) return false;
    const after = this.tokens[i + 1];
    if (!after) return true;
    return (
      after.kind === TokenKind.EOF ||
      after.newlineBefore ||
      this.isPunct(':', after) ||
      this.isPunct('=', after) ||
      this.isPunct(';', after) ||
      this.isPunct('}', after) ||
      this.isPunct(',', after)
    );
  }

  /** `name(` … `)` followed by `{` or `:` */
  private isMethodLike(): boolean {
    if (!this.isIdent() || !this.isPunct('(', this.peek())) return false;
    let i = this.pos + 1;
    let depth = 0;
    while (i < this.tokens.length) {
      const t = this.tokens[i];
      if (this.isPunct('(', t)) depth++;
      else if (this.isPunct(')', t)) {
        depth--;
        if (depth === 0) break;
      } else if (t.kind === TokenKind.EOF) return false;
      i++;
    }
    const after = this.tokens[i + 1];
    return !!after && (this.isPunct('{', after) || this.isPunct(':', after));
  }

  private parseMethod(
    declStart: number,
    annotations: ast.Annotation[],
    modifiers: ast.ModifierTok[],
    hasKeyword: boolean,
    isConstructor: boolean,
  ): ast.MethodDecl {
    let keywordSpan: Span | undefined;
    let name = '';
    let nameSpan: Span;

    if (isConstructor) {
      const t = this.advance();
      keywordSpan = { start: t.start, end: t.end };
      name = 'construct';
      nameSpan = keywordSpan;
    } else {
      if (hasKeyword) {
        const t = this.advance();
        keywordSpan = { start: t.start, end: t.end };
      }
      if (this.isIdent() && !(hasKeyword && this.cur.newlineBefore)) {
        const t = this.advance();
        name = t.value;
        nameSpan = { start: t.start, end: t.end };
      } else {
        if (this.cur.kind === TokenKind.Identifier && RESERVED_WORDS.has(this.cur.value)) {
          this.error(`'${this.cur.value}' is a reserved word and cannot be used as a method name`);
        } else {
          this.error("Expected method name after 'method'");
        }
        nameSpan = this.emptySpanHere();
      }
    }

    const params = this.isPunct('(') ? this.parseParams() : [];

    let returnType: ast.TypeRef | undefined;
    if (this.isPunct(':')) {
      this.advance();
      returnType = this.parseType();
    }

    let body: ast.Block | undefined;
    if (this.isPunct('{')) {
      body = this.parseBlock();
    }

    return {
      kind: 'Method',
      name,
      nameSpan,
      params,
      returnType,
      body,
      annotations,
      modifiers,
      doc: this.docFor(declStart),
      isConstructor,
      hasKeyword,
      keywordSpan,
      start: declStart,
      end: this.prevEnd(),
    };
  }

  private parseParams(): ast.Param[] {
    const params: ast.Param[] = [];
    this.expectPunct('(');
    while (!this.isPunct(')') && !this.atEOF()) {
      const startPos = this.pos;
      const p = this.parseParam();
      if (p) params.push(p);
      if (this.isPunct(',')) {
        this.advance();
        continue;
      }
      if (this.isPunct(')')) break;
      if (this.isPunct('{') || (this.cur.newlineBefore && this.isStatementStart())) break;
      this.unexpected("in parameter list, expected ',' or ')'");
      if (this.pos === startPos) this.advance();
    }
    this.expectPunct(')', 'to close parameter list');
    return params;
  }

  private parseParam(): ast.Param | undefined {
    if (!this.isIdent()) {
      this.error(`Expected parameter type, found ${this.describeToken()}`);
      return undefined;
    }
    const start = this.cur.start;
    const type = this.parseType();
    if (this.isIdent() && !this.cur.newlineBefore) {
      const t = this.advance();
      return { kind: 'Param', type, name: t.value, nameSpan: { start: t.start, end: t.end }, start, end: t.end };
    }
    this.error(`Expected parameter name after type '${type.name}'`, { start: type.start, end: type.end });
    return { kind: 'Param', type, name: '', nameSpan: { start: type.end, end: type.end }, start, end: type.end };
  }

  parseType(): ast.TypeRef {
    if (!this.isIdent()) {
      this.error(`Expected type, found ${this.describeToken()}`);
      const at = this.emptySpanHere();
      return { kind: 'TypeRef', name: '', nameSpan: at, args: [], start: at.start, end: at.end };
    }
    const first = this.advance();
    let name = first.value;
    let nameEnd = first.end;
    while (this.isPunct('.') && this.peek().kind === TokenKind.Identifier) {
      this.advance();
      const part = this.advance();
      name += '.' + part.value;
      nameEnd = part.end;
    }
    const args: ast.TypeRef[] = [];
    if (this.isPunct('<') && !this.cur.newlineBefore) {
      this.advance();
      while (!this.isPunct('>') && !this.atEOF()) {
        const startPos = this.pos;
        args.push(this.parseType());
        if (this.isPunct(',')) {
          this.advance();
          continue;
        }
        if (!this.isPunct('>')) {
          if (this.pos === startPos) break;
          if (this.cur.newlineBefore) break;
        }
      }
      this.expectPunct('>', 'to close type arguments');
    }
    return { kind: 'TypeRef', name, nameSpan: { start: first.start, end: nameEnd }, args, start: first.start, end: this.prevEnd() };
  }

  private parseDefine(declStart: number, annotations: ast.Annotation[], modifiers: ast.ModifierTok[]): ast.DefineDecl {
    this.advance(); // define
    let defineKind: ast.DefineKind = 'class';
    if (this.cur.kind === TokenKind.Identifier && DEFINE_KINDS.has(this.cur.value)) {
      defineKind = this.advance().value as ast.DefineKind;
    } else {
      this.error("Expected 'class', 'enum', 'interface', 'abstract' or 'annotation' after 'define'");
    }

    let name = '';
    let nameSpan: Span = this.emptySpanHere();
    if (this.isIdent()) {
      const t = this.advance();
      name = t.value;
      nameSpan = { start: t.start, end: t.end };
    } else {
      this.error(`Expected ${defineKind} name`);
    }

    const params = this.isPunct('(') ? this.parseParams() : [];

    const bases: ast.TypeRef[] = [];
    if (this.isKw('based')) {
      this.advance();
      if (!this.isIdent()) this.error("Expected base type after 'based'");
      while (this.isIdent()) {
        bases.push(this.parseType());
        if (this.isPunct(',')) this.advance();
        else break;
      }
    }

    let constants: ast.EnumConstant[] = [];
    const members: ast.Member[] = [];
    let bodySpan: Span | undefined;

    if (this.isPunct('{')) {
      const open = this.advance();
      if (defineKind === 'enum') constants = this.parseEnumConstants();
      while (!this.isPunct('}') && !this.atEOF()) {
        this.skipSemis();
        if (this.isPunct('}')) break;
        const startPos = this.pos;
        const member = this.parseMember();
        if (member) members.push(member);
        if (this.pos === startPos) {
          this.unexpected(`in ${defineKind} body, expected a member declaration`);
          this.advance();
        }
      }
      const close = this.expectPunct('}', `to close ${defineKind} '${name}'`);
      bodySpan = { start: open.start, end: close ? close.end : this.prevEnd() };
    } else {
      this.error(`Expected '{' to start the body of ${defineKind} '${name}'`);
    }

    return {
      kind: 'Define',
      defineKind,
      name,
      nameSpan,
      params,
      bases,
      constants,
      members,
      bodySpan,
      annotations,
      modifiers,
      doc: this.docFor(declStart),
      start: declStart,
      end: this.prevEnd(),
    };
  }

  private parseEnumConstants(): ast.EnumConstant[] {
    const list: ast.EnumConstant[] = [];
    while (this.isIdent() && !this.isMethodLike() && !this.looksLikeTypedDecl() && !this.isPunct('=', this.peek()) && !this.isPunct(':', this.peek())) {
      const t = this.advance();
      let args: ast.Expr[] = [];
      if (this.isPunct('(') && !this.cur.newlineBefore) {
        this.advance();
        args = this.parseArgs(')');
      }
      list.push({ kind: 'EnumConstant', name: t.value, nameSpan: { start: t.start, end: t.end }, args, start: t.start, end: this.prevEnd() });
      if (this.isPunct(',')) {
        this.advance();
        continue;
      }
      break;
    }
    if (this.isPunct(';')) this.advance();
    return list;
  }

  private parseMember(): ast.Member | undefined {
    const declStart = this.cur.start;
    const annotations = this.parseAnnotations();
    const modifiers = this.parseModifiers();

    if (this.isKw('method')) return this.parseMethod(declStart, annotations, modifiers, true, false);
    if (this.isKw('construct')) return this.parseMethod(declStart, annotations, modifiers, true, true);
    if (this.isKw('define')) return this.parseDefine(declStart, annotations, modifiers);
    if (this.isKw('var')) return this.parseVar(declStart, annotations, modifiers);
    if (this.looksLikeTypedDecl()) return this.parseField(declStart, annotations, modifiers);
    if (this.isIdent() && this.isPunct('(', this.peek())) return this.parseMethod(declStart, annotations, modifiers, false, false);
    if (this.isIdent() && (this.isPunct('=', this.peek()) || this.isPunct(':', this.peek()))) {
      return this.parseField(declStart, annotations, modifiers);
    }

    if (annotations.length || modifiers.length) {
      this.error(`Expected a member declaration after ${annotations.length ? 'annotation' : 'modifier'}, found ${this.describeToken()}`);
    }
    return undefined;
  }

  private parseVar(declStart: number, annotations: ast.Annotation[], modifiers: ast.ModifierTok[]): ast.FieldDecl {
    const kw = this.advance();
    let name = '';
    let nameSpan: Span = this.emptySpanHere();
    if (this.isIdent() && !this.cur.newlineBefore) {
      const t = this.advance();
      name = t.value;
      nameSpan = { start: t.start, end: t.end };
    } else if (this.cur.kind === TokenKind.Identifier && RESERVED_WORDS.has(this.cur.value) && !this.cur.newlineBefore) {
      this.error(`'${this.cur.value}' is a reserved word and cannot be used as a variable name`);
      const t = this.advance();
      name = t.value;
      nameSpan = { start: t.start, end: t.end };
    } else {
      this.error("Expected variable name after 'var'");
    }

    let type: ast.TypeRef | undefined;
    if (this.isPunct(':')) {
      this.advance();
      type = this.parseType();
    }

    let value: ast.Expr | undefined;
    if (this.isPunct('=')) {
      this.advance();
      value = this.parseExpression();
      if (!value) this.error(`Expected an expression after '=' in declaration of '${name}'`);
    }

    return {
      kind: 'Field',
      name,
      nameSpan,
      type,
      accessors: [],
      value,
      isVar: true,
      varSpan: { start: kw.start, end: kw.end },
      annotations,
      modifiers,
      doc: this.docFor(declStart),
      start: declStart,
      end: this.prevEnd(),
    };
  }

  private parseField(declStart: number, annotations: ast.Annotation[], modifiers: ast.ModifierTok[]): ast.FieldDecl {
    let type: ast.TypeRef | undefined;
    if (this.looksLikeTypedDecl()) type = this.parseType();

    let name = '';
    let nameSpan: Span = this.emptySpanHere();
    if (this.isIdent()) {
      const t = this.advance();
      name = t.value;
      nameSpan = { start: t.start, end: t.end };
    } else {
      this.error('Expected field name');
    }

    const accessors: ast.Accessor[] = [];
    if (this.isPunct(':')) {
      this.advance();
      if (this.isKw('get') || this.isKw('set')) {
        for (;;) {
          const t = this.advance();
          accessors.push({ name: t.value as 'get' | 'set', start: t.start, end: t.end });
          if (this.isPunct(',') && (this.isKw('get', this.peek()) || this.isKw('set', this.peek()))) {
            this.advance();
            continue;
          }
          break;
        }
      } else if (!type) {
        type = this.parseType();
      } else {
        this.error(`Expected 'get' or 'set' after ':', found ${this.describeToken()}`);
      }
    }

    let value: ast.Expr | undefined;
    if (this.isPunct('=')) {
      this.advance();
      value = this.parseExpression();
      if (!value) this.error(`Expected an expression after '=' in declaration of '${name}'`);
    }

    return {
      kind: 'Field',
      name,
      nameSpan,
      type,
      accessors,
      value,
      isVar: false,
      annotations,
      modifiers,
      doc: this.docFor(declStart),
      start: declStart,
      end: this.prevEnd(),
    };
  }

  // -------------------------------------------------------------- statements

  parseBlock(): ast.Block {
    const open = this.expectPunct('{');
    const start = open ? open.start : this.cur.start;
    const statements: ast.Stmt[] = [];
    while (!this.isPunct('}') && !this.atEOF()) {
      this.skipSemis();
      if (this.isPunct('}')) break;
      const startPos = this.pos;
      const stmt = this.parseStatement();
      if (stmt) statements.push(stmt);
      if (this.pos === startPos) {
        this.unexpected('in block, expected a statement');
        this.advance();
      }
    }
    this.expectPunct('}', 'to close block');
    return { kind: 'Block', statements, start, end: this.prevEnd() };
  }

  private parseStatement(): ast.Stmt | undefined {
    const start = this.cur.start;

    if (this.isKw('var')) return this.parseVar(start, [], []);
    // `Type name = value` – typed local declaration
    if (this.looksLikeTypedDecl()) return this.parseField(start, [], []);
    if (this.isModifier()) {
      const modifiers = this.parseModifiers();
      if (this.isKw('var')) return this.parseVar(start, [], modifiers);
      if (this.looksLikeTypedDecl()) return this.parseField(start, [], modifiers);
      this.error(`Expected 'var' after modifier`);
      return undefined;
    }
    if (this.isKw('println')) {
      const kw = this.advance();
      let value: ast.Expr | undefined;
      if (this.cur.newlineBefore || this.isPunct('}') || this.isPunct(';') || this.atEOF()) {
        this.error("Expected a value after 'println'", kw);
      } else {
        value = this.parseExpression();
        if (!value) this.error("Expected a value after 'println'", kw);
      }
      return { kind: 'Print', value, start, end: this.prevEnd() };
    }
    if (this.isKw('return')) {
      this.advance();
      let value: ast.Expr | undefined;
      if (!(this.cur.newlineBefore || this.isPunct('}') || this.isPunct(';') || this.atEOF())) {
        value = this.parseExpression();
      }
      return { kind: 'Return', value, start, end: this.prevEnd() };
    }
    if (this.isKw('if')) return this.parseIf();
    if (this.isKw('for')) return this.parseFor();
    if (this.isKw('while')) return this.parseWhile();
    if (this.isKw('break')) {
      this.advance();
      return { kind: 'Break', start, end: this.prevEnd() };
    }
    if (this.isKw('continue')) {
      this.advance();
      return { kind: 'Continue', start, end: this.prevEnd() };
    }
    if (this.isKw('else')) {
      this.error("'else' without a preceding 'if'");
      this.advance();
      return undefined;
    }
    if (this.isPunct('{')) return this.parseBlock();
    if (this.isPunct('}')) return undefined;

    if (this.isKw('method') || this.isKw('define') || this.isKw('construct') || this.isPunct('@')) {
      const what = this.isPunct('@') ? 'Annotated declarations' : `'${this.cur.value}' declarations`;
      this.error(`${what} are not allowed inside a method body`);
      const annotations = this.parseAnnotations();
      if (this.isKw('method')) return this.parseMethod(start, annotations, [], true, false);
      if (this.isKw('construct')) return this.parseMethod(start, annotations, [], true, true);
      if (this.isKw('define')) return this.parseDefine(start, annotations, []);
      return undefined;
    }
    if (this.isKw('package') || this.isKw('import')) {
      this.error(`'${this.cur.value}' is only allowed at the top of the file`);
      if (this.isKw('package')) this.parsePackage();
      else this.parseImport();
      return undefined;
    }

    const expr = this.parseExpression();
    if (!expr) return undefined;

    if (this.isPunct('=')) {
      const op = this.advance();
      const value = this.parseExpression();
      if (!value) this.error("Expected an expression after '='");
      if (expr.kind !== 'Ident' && expr.kind !== 'Member' && expr.kind !== 'Index') {
        this.error('Invalid assignment target', expr);
      }
      return { kind: 'Assign', target: expr, opSpan: { start: op.start, end: op.end }, value, start, end: this.prevEnd() };
    }

    if (expr.kind !== 'Call' && expr.kind !== 'StructInit' && expr.kind !== 'Member' && expr.kind !== 'Ident') {
      // e.g. `sum + item.price()` – legal but has no effect; keep it as an expression statement.
    }
    return { kind: 'ExprStmt', expr, start, end: this.prevEnd() };
  }

  private parseCondition(keyword: string): ast.Expr | undefined {
    const hadParen = this.isPunct('(');
    if (hadParen) this.advance();
    else this.error(`Expected '(' after '${keyword}'`);
    const cond = this.parseExpression();
    if (!cond) this.error(`Expected condition after '${keyword}'`);
    if (hadParen) this.expectPunct(')', `to close '${keyword}' condition`);
    return cond;
  }

  private parseBody(): ast.Stmt | undefined {
    if (this.isPunct('{')) return this.parseBlock();
    if (this.atEOF() || this.isPunct('}')) {
      this.error("Expected '{'");
      return undefined;
    }
    return this.parseStatement();
  }

  private parseIf(): ast.IfStmt {
    const start = this.advance().start;
    const cond = this.parseCondition('if');
    const then = this.parseBody();
    let elseBranch: ast.Stmt | undefined;
    if (this.isKw('else')) {
      this.advance();
      elseBranch = this.isKw('if') ? this.parseIf() : this.parseBody();
    }
    return { kind: 'If', cond, then, else: elseBranch, start, end: this.prevEnd() };
  }

  private parseFor(): ast.ForStmt {
    const start = this.advance().start;
    const node: ast.ForStmt = { kind: 'For', start, end: start };
    const hadParen = this.isPunct('(');
    if (hadParen) this.advance();
    else this.error("Expected '(' after 'for'");

    if (this.isIdent() && this.isKw('in', this.peek())) {
      const v = this.advance();
      node.varName = v.value;
      node.varSpan = { start: v.start, end: v.end };
      this.advance(); // in
      node.iterable = this.parseExpression();
      if (!node.iterable) this.error("Expected an expression after 'in'");
    } else {
      // C-style loop: for (init; cond; update)
      if (!this.isPunct(';')) node.init = this.parseStatement();
      if (!node.init && !this.isPunct(';')) this.error("Expected 'item in collection' or an initializer in 'for'");
      this.expectPunct(';', "in 'for' header");
      if (!this.isPunct(';')) node.cond = this.parseExpression();
      this.expectPunct(';', "in 'for' header");
      if (!this.isPunct(')')) node.update = this.parseStatement();
    }
    if (hadParen) this.expectPunct(')', "to close 'for' header");
    node.body = this.parseBody();
    node.end = this.prevEnd();
    return node;
  }

  private parseWhile(): ast.WhileStmt {
    const start = this.advance().start;
    const cond = this.parseCondition('while');
    const body = this.parseBody();
    return { kind: 'While', cond, body, start, end: this.prevEnd() };
  }

  // ------------------------------------------------------------- expressions

  parseExpression(): ast.Expr | undefined {
    return this.parseBinary(0);
  }

  private static readonly PRECEDENCE: Record<string, number> = {
    '||': 1,
    '&&': 2,
    '==': 3,
    '!=': 3,
    '<': 4,
    '>': 4,
    '<=': 4,
    '>=': 4,
    '+': 5,
    '-': 5,
    '*': 6,
    '/': 6,
    '%': 6,
  };

  private parseBinary(minPrec: number): ast.Expr | undefined {
    let left = this.parseUnary();
    if (!left) return undefined;
    for (;;) {
      const t = this.cur;
      if (t.kind !== TokenKind.Punct) break;
      const prec = Parser.PRECEDENCE[t.value];
      if (prec === undefined || prec <= minPrec) break;
      // An operator on a new line starts a new statement (Novus has no semicolons).
      if (t.newlineBefore) break;
      this.advance();
      const right = this.parseBinary(prec);
      if (!right) this.error(`Expected an expression after '${t.value}'`);
      left = { kind: 'Binary', op: t.value, opSpan: { start: t.start, end: t.end }, left, right, start: left.start, end: this.prevEnd() };
    }
    return left;
  }

  private parseUnary(): ast.Expr | undefined {
    if (this.isPunct('-') || this.isPunct('!')) {
      const t = this.advance();
      const operand = this.parseUnary();
      if (!operand) this.error(`Expected an expression after '${t.value}'`);
      return { kind: 'Unary', op: t.value, operand, start: t.start, end: this.prevEnd() };
    }
    return this.parsePostfix();
  }

  private parsePostfix(): ast.Expr | undefined {
    let expr = this.parsePrimary();
    if (!expr) return undefined;
    for (;;) {
      if (this.isPunct('.')) {
        this.advance();
        let name = '';
        let nameSpan: Span = this.emptySpanHere();
        if (this.cur.kind === TokenKind.Identifier && !this.cur.newlineBefore) {
          const t = this.advance();
          name = t.value;
          nameSpan = { start: t.start, end: t.end };
        } else {
          this.error("Expected member name after '.'");
        }
        expr = { kind: 'Member', object: expr, name, nameSpan, start: expr.start, end: this.prevEnd() };
        continue;
      }
      if (this.isPunct('(') && !this.cur.newlineBefore) {
        const open = this.advance();
        const args = this.parseArgs(')');
        expr = { kind: 'Call', callee: expr, args, parenStart: open.start, parenEnd: this.prevEnd(), start: expr.start, end: this.prevEnd() };
        continue;
      }
      if (this.isPunct('[') && !this.cur.newlineBefore) {
        this.advance();
        const index = this.parseExpression();
        if (!index) this.error('Expected index expression');
        this.expectPunct(']', 'to close index');
        expr = { kind: 'Index', object: expr, index, start: expr.start, end: this.prevEnd() };
        continue;
      }
      if (this.isPunct('{') && !this.cur.newlineBefore && (expr.kind === 'Ident' || expr.kind === 'Member')) {
        this.advance();
        const args = this.parseNamedArgs('}');
        expr = { kind: 'StructInit', target: expr, args, start: expr.start, end: this.prevEnd() };
        continue;
      }
      break;
    }
    return expr;
  }

  private parseArgs(closer: string): ast.Expr[] {
    const args: ast.Expr[] = [];
    while (!this.isPunct(closer) && !this.atEOF()) {
      const startPos = this.pos;
      const e = this.parseExpression();
      if (e) args.push(e);
      if (this.isPunct(',')) {
        this.advance();
        continue;
      }
      if (this.isPunct(closer)) break;
      if (this.cur.newlineBefore && (this.isStatementStart() || this.isPunct('}'))) break;
      this.unexpected(`in argument list, expected ',' or '${closer}'`);
      if (this.pos === startPos) this.advance();
    }
    this.expectPunct(closer, 'to close argument list');
    return args;
  }

  private parsePrimary(): ast.Expr | undefined {
    const t = this.cur;
    switch (t.kind) {
      case TokenKind.Integer:
        this.advance();
        return { kind: 'Int', value: parseInt(t.value, 10), start: t.start, end: t.end };
      case TokenKind.Float:
        this.advance();
        return { kind: 'Float', value: parseFloat(t.value), start: t.start, end: t.end };
      case TokenKind.String: {
        this.advance();
        const interpolations: ast.Expr[] = [];
        for (const part of t.parts ?? []) {
          if (part.kind !== 'interp') continue;
          const e = this.parseInterpolation(part.value, part.start, part.end);
          if (e) interpolations.push(e);
        }
        return { kind: 'String', value: t.value, interpolations, start: t.start, end: t.end };
      }
      case TokenKind.Identifier: {
        if (t.value === 'true' || t.value === 'false') {
          this.advance();
          return { kind: 'Bool', value: t.value === 'true', start: t.start, end: t.end };
        }
        if (t.value === 'null') {
          this.advance();
          return { kind: 'Null', start: t.start, end: t.end };
        }
        if (t.value === 'this') {
          this.advance();
          return { kind: 'This', start: t.start, end: t.end };
        }
        if (RESERVED_WORDS.has(t.value)) {
          this.error(`Unexpected keyword '${t.value}'`);
          return undefined;
        }
        this.advance();
        return { kind: 'Ident', name: t.value, start: t.start, end: t.end };
      }
      case TokenKind.Punct: {
        if (t.value === '(') {
          this.advance();
          const inner = this.parseExpression();
          if (!inner) this.error("Expected an expression after '('");
          this.expectPunct(')', 'to close parenthesized expression');
          return { kind: 'Paren', expr: inner, start: t.start, end: this.prevEnd() };
        }
        if (t.value === '[') {
          this.advance();
          const elements = this.parseArgs(']');
          return { kind: 'Array', elements, start: t.start, end: this.prevEnd() };
        }
        if (t.value === '{') {
          return this.parseMapLiteral();
        }
        break;
      }
      default:
        break;
    }
    this.error(`Expected an expression, found ${this.describeToken()}`);
    return undefined;
  }

  private parseMapLiteral(): ast.MapLit {
    const open = this.advance();
    const entries: ast.MapEntry[] = [];
    while (!this.isPunct('}') && !this.atEOF()) {
      const startPos = this.pos;
      const key = this.parseExpression();
      if (key) {
        let value: ast.Expr | undefined;
        if (this.expectPunct(':', 'after map key')) {
          value = this.parseExpression();
          if (!value) this.error('Expected a value after \':\'');
        }
        entries.push({ key, value, start: key.start, end: this.prevEnd() });
      }
      if (this.isPunct(',')) {
        this.advance();
        continue;
      }
      if (this.isPunct('}')) break;
      if (this.cur.newlineBefore && this.isStatementStart()) break;
      this.unexpected("in map literal, expected ',' or '}'");
      if (this.pos === startPos) this.advance();
    }
    this.expectPunct('}', 'to close map literal');
    return { kind: 'Map', entries, start: open.start, end: this.prevEnd() };
  }

  private parseInterpolation(source: string, start: number, end: number): ast.Expr | undefined {
    const lexed = lex(source, start);
    this.errors.push(...lexed.errors);
    const sub = new Parser(this.src, lexed.tokens, [], this.lineMap);
    const expr = sub.parseExpression();
    if (!expr) {
      if (!sub.errors.length) sub.error('Expected an expression inside ${...}', { start, end });
    } else if (!sub.atEOF()) {
      sub.error(`Unexpected ${sub.describeToken()} inside \${...}`);
    }
    this.errors.push(...sub.errors);
    return expr;
  }
}
