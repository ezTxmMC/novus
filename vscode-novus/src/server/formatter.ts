/**
 * Token based formatter for Novus.
 *
 * Normalizes indentation, spacing and blank lines while preserving the
 * author's line breaks, comments and string contents. It works on the token
 * stream only, so it also formats files that do not parse yet.
 */
import { Range, TextEdit } from 'vscode-languageserver/node';
import { RESERVED_WORDS, TokenKind, lex } from './lexer';
import { LineMap } from './parser';

export interface FormatOptions {
  tabSize: number;
  insertSpaces: boolean;
  /** Spacing around `=` in `Type{ name=value }` and `@Annotation{ key=value }`. */
  namedArgumentSpacing: 'none' | 'spaces';
  /** Maximum number of consecutive blank lines to keep. */
  maxBlankLines: number;
}

export const DEFAULT_FORMAT_OPTIONS: FormatOptions = { tabSize: 2, insertSpaces: true, namedArgumentSpacing: 'none', maxBlankLines: 1 };

type Kind = 'ident' | 'number' | 'string' | 'punct' | 'comment';

interface FTok {
  kind: Kind;
  value: string;
  start: number;
  end: number;
  line: number;
  endLine: number;
  block: boolean;
  generic: boolean;
  unary: boolean;
  namedArg: boolean;
}

type BraceKind = 'block' | 'init' | 'map' | 'paren' | 'bracket';

interface Open {
  ch: string;
  kind: BraceKind;
  lineLevel: number;
}

/** One formatted output line (a multi-line block comment counts as one entry). */
export interface FormattedLine {
  origLine: number;
  origEndLine: number;
  blankBefore: number;
  text: string;
}

const CONTROL_KW = new Set(['if', 'for', 'while', 'return', 'println', 'print', 'eprintln', 'else', 'in', 'sync', 'await']);
const HEAD_KW = new Set(['method', 'define', 'construct', 'abstract', 'else', 'if', 'for', 'while', 'sync', 'async']);
const MODIFIERS = new Set(['private', 'public', 'protected', 'final', 'static', 'abstract']);
const BINARY = new Set(['+', '-', '*', '/', '%', '==', '!=', '<', '>', '<=', '>=', '&&', '||', '=']);
const OPENERS: Record<string, string> = { '{': '}', '(': ')', '[': ']' };
const CLOSERS = new Set(['}', ')', ']']);

export function formatNovus(text: string, opts: FormatOptions): string {
  const lines = formatLines(text, opts);
  if (!lines.length) return text.trim().length ? text : '';
  const out: string[] = [];
  for (const l of lines) {
    for (let i = 0; i < l.blankBefore; i++) out.push('');
    out.push(l.text);
  }
  return out.join('\n') + '\n';
}

export function formatLines(text: string, opts: FormatOptions): FormattedLine[] {
  const lineMap = new LineMap(text);
  const toks = tokenize(text, lineMap);
  if (!toks.length) return [];
  markGenerics(toks);
  const groups = groupLines(toks);

  const indentUnit = opts.insertSpaces ? ' '.repeat(Math.max(1, opts.tabSize)) : '\t';
  const indent = (level: number): string => indentUnit.repeat(Math.max(0, level));
  const width = (s: string): number => {
    let w = 0;
    for (const c of s) w += c === '\t' ? opts.tabSize : 1;
    return w;
  };

  const result: FormattedLine[] = [];
  const stack: Open[] = [];
  let prevEndLine = -1;
  let prevEndsOpener = false;
  let prevEndsOperator = false;

  for (const group of groups) {
    const first = group[0];
    const startsWithCloser = first.kind === 'punct' && CLOSERS.has(first.value);
    let blankBefore = prevEndLine < 0 ? 0 : Math.min(Math.max(0, first.line - prevEndLine - 1), opts.maxBlankLines);
    if (prevEndsOpener || startsWithCloser) blankBefore = 0;

    let level: number;
    if (startsWithCloser) {
      const top = stack[stack.length - 1];
      level = top ? top.lineLevel : 0;
    } else {
      level = stack.length ? stack[stack.length - 1].lineLevel + 1 : 0;
      if (prevEndsOperator || (first.kind === 'punct' && first.value === '.')) level += 1;
    }

    const head = lineHead(group);
    let s = indent(level);
    let parensOnLine = 0;
    let lastSignificant: FTok | undefined;

    for (let i = 0; i < group.length; i++) {
      const tok = group[i];
      const prev = i > 0 ? group[i - 1] : undefined;

      if (tok.kind === 'comment') {
        if (prev) s += ' ';
        s += commentText(tok, text, lineMap, level, indent, width);
        continue;
      }

      if (tok.kind === 'punct' && (tok.value === '-' || tok.value === '!')) tok.unary = isUnary(prev);
      let braceKind: BraceKind | undefined;
      if (tok.kind === 'punct' && tok.value === '{') braceKind = classifyBrace(prev, head, parensOnLine > 0);
      if (tok.kind === 'punct' && tok.value === '=') {
        const top = stack[stack.length - 1];
        tok.namedArg = !!top && top.kind === 'init';
      }

      if (prev && needSpace(prev, tok, stack, braceKind, opts)) s += ' ';
      s += tok.value;

      if (tok.kind === 'punct' && OPENERS[tok.value]) {
        const kind: BraceKind = tok.value === '(' ? 'paren' : tok.value === '[' ? 'bracket' : braceKind ?? 'block';
        if (tok.value === '(') parensOnLine++;
        stack.push({ ch: tok.value, kind, lineLevel: level });
      } else if (tok.kind === 'punct' && CLOSERS.has(tok.value)) {
        if (tok.value === ')') parensOnLine = Math.max(0, parensOnLine - 1);
        const idx = findMatching(stack, tok.value);
        if (idx >= 0) stack.length = idx;
      }
      lastSignificant = tok;
    }

    const last = group[group.length - 1];
    result.push({ origLine: first.line, origEndLine: last.endLine, blankBefore, text: s.trimEnd() });
    prevEndLine = last.endLine;
    prevEndsOpener = !!lastSignificant && lastSignificant.kind === 'punct' && !!OPENERS[lastSignificant.value];
    prevEndsOperator = !!lastSignificant && lastSignificant.kind === 'punct' && BINARY.has(lastSignificant.value);
  }
  return result;
}

// ------------------------------------------------------------------ edits

/** Minimal edit list turning `text` into `formatted` (common prefix/suffix lines are kept). */
export function fullFormatEdits(text: string, formatted: string, lineMap: LineMap): TextEdit[] {
  if (text === formatted) return [];
  const a = text.split('\n');
  const b = formatted.split('\n');
  let prefix = 0;
  while (prefix < a.length && prefix < b.length && a[prefix] === b[prefix]) prefix++;
  let suffix = 0;
  while (suffix < a.length - prefix && suffix < b.length - prefix && a[a.length - 1 - suffix] === b[b.length - 1 - suffix]) suffix++;
  const startLine = prefix;
  const endLine = a.length - suffix; // exclusive
  const replacement = b.slice(prefix, b.length - suffix);
  const start = { line: startLine, character: 0 };
  const end = endLine >= a.length ? lineMap.positionOf(text.length) : { line: endLine, character: 0 };
  let newText = replacement.join('\n');
  if (endLine < a.length) newText += '\n';
  return [{ range: { start, end }, newText }];
}

/** Edits for the lines overlapping `range` only (each formatted line replaces its original line). */
export function rangeFormatEdits(text: string, lines: FormattedLine[], lineMap: LineMap, range: Range): TextEdit[] {
  const edits: TextEdit[] = [];
  const lineText = (line: number): string => {
    const start = lineMap.lineStart(line);
    const end = line + 1 < lineMap.lineCount ? lineMap.lineStart(line + 1) - 1 : text.length;
    return text.slice(start, Math.max(start, end)).replace(/\r$/, '');
  };
  for (let i = 0; i < lines.length; i++) {
    const l = lines[i];
    if (l.origEndLine < range.start.line || l.origLine > range.end.line) continue;
    const prev = i > 0 ? lines[i - 1] : undefined;
    const prevInRange = !!prev && prev.origEndLine >= range.start.line;
    const endChar = lineText(l.origEndLine).length;
    if (prev && prevInRange) {
      const from = { line: prev.origEndLine, character: lineText(prev.origEndLine).length };
      const to = { line: l.origEndLine, character: endChar };
      edits.push({ range: { start: from, end: to }, newText: '\n'.repeat(l.blankBefore + 1) + l.text });
    } else {
      edits.push({ range: { start: { line: l.origLine, character: 0 }, end: { line: l.origEndLine, character: endChar } }, newText: l.text });
    }
  }
  return edits;
}

// ---------------------------------------------------------------- helpers

function tokenize(text: string, lineMap: LineMap): FTok[] {
  const lexed = lex(text);
  const toks: FTok[] = [];
  for (const t of lexed.tokens) {
    if (t.kind === TokenKind.EOF) continue;
    const kind: Kind = t.kind === TokenKind.Identifier ? 'ident' : t.kind === TokenKind.String ? 'string' : t.kind === TokenKind.Punct ? 'punct' : 'number';
    toks.push(mk(kind, text.slice(t.start, t.end), t.start, t.end, lineMap, false));
  }
  for (const c of lexed.comments) toks.push(mk('comment', c.text, c.start, c.end, lineMap, c.block));
  toks.sort((x, y) => x.start - y.start);
  return toks;
}

function mk(kind: Kind, value: string, start: number, end: number, lineMap: LineMap, block: boolean): FTok {
  return { kind, value, start, end, line: lineMap.lineOf(start), endLine: lineMap.lineOf(Math.max(start, end - 1)), block, generic: false, unary: false, namedArg: false };
}

/** Marks `<` … `>` pairs that form type arguments (`array<Person>`). */
function markGenerics(toks: FTok[]): void {
  for (let i = 1; i < toks.length; i++) {
    const t = toks[i];
    if (t.kind !== 'punct' || t.value !== '<' || t.generic) continue;
    const prev = toks[i - 1];
    if (prev.kind !== 'ident' || RESERVED_WORDS.has(prev.value) || prev.end !== t.start) continue;
    let depth = 0;
    const angles: number[] = [];
    let ok = false;
    for (let j = i; j < toks.length; j++) {
      const u = toks[j];
      if (u.line !== t.line) break;
      if (u.kind === 'punct' && u.value === '<') {
        depth++;
        angles.push(j);
      } else if (u.kind === 'punct' && u.value === '>') {
        depth--;
        angles.push(j);
        if (depth === 0) {
          ok = true;
          break;
        }
      } else if (u.kind === 'ident' || (u.kind === 'punct' && (u.value === ',' || u.value === '.'))) {
        continue;
      } else {
        break;
      }
    }
    if (ok) for (const j of angles) toks[j].generic = true;
  }
}

function groupLines(toks: FTok[]): FTok[][] {
  const groups: FTok[][] = [];
  let current: FTok[] = [];
  let currentEnd = -1;
  let forceBreak = false;
  for (const t of toks) {
    if (current.length && (t.line > currentEnd || forceBreak)) {
      groups.push(current);
      current = [];
    }
    current.push(t);
    currentEnd = t.endLine;
    forceBreak = t.kind === 'comment' && t.endLine > t.line;
  }
  if (current.length) groups.push(current);
  return groups;
}

/** First meaningful identifier of a line (skipping closers and modifiers); '@' for annotation lines. */
function lineHead(group: FTok[]): string | undefined {
  for (const t of group) {
    if (t.kind === 'comment') continue;
    if (t.kind === 'punct') {
      if (CLOSERS.has(t.value)) continue;
      if (t.value === '@') return '@';
      return undefined;
    }
    if (t.kind === 'ident') {
      if (MODIFIERS.has(t.value) && t.value !== 'abstract') continue;
      return t.value;
    }
    return undefined;
  }
  return undefined;
}

function classifyBrace(prev: FTok | undefined, head: string | undefined, insideParen: boolean): BraceKind {
  if (!prev) return 'block';
  if (prev.kind === 'punct') {
    if (prev.value === ')') return 'block';
    if (prev.value === '>' && prev.generic) return head && HEAD_KW.has(head) ? 'block' : 'init';
    return 'map';
  }
  if (prev.kind === 'ident') {
    if (prev.value === 'else') return 'block';
    if (RESERVED_WORDS.has(prev.value)) return 'map';
    if (head && HEAD_KW.has(head) && !insideParen) return 'block';
    return 'init';
  }
  return 'map';
}

function isUnary(prev: FTok | undefined): boolean {
  if (!prev) return true;
  if (prev.kind === 'number' || prev.kind === 'string') return false;
  if (prev.kind === 'ident') return CONTROL_KW.has(prev.value);
  if (prev.kind === 'punct') {
    if (prev.value === ')' || prev.value === ']') return false;
    if (prev.value === '>' && prev.generic) return false;
    return true;
  }
  return false;
}

function findMatching(stack: Open[], closer: string): number {
  for (let i = stack.length - 1; i >= 0; i--) {
    if (OPENERS[stack[i].ch] === closer) return i;
  }
  return -1;
}

function needSpace(prev: FTok, cur: FTok, stack: Open[], braceKind: BraceKind | undefined, opts: FormatOptions): boolean {
  const p = prev.value;
  const c = cur.value;
  const pp = prev.kind === 'punct';
  const cp = cur.kind === 'punct';
  const top = stack[stack.length - 1];

  if (prev.kind === 'comment') return true;
  // `name=value` inside object initializers / annotation arguments
  if (pp && p === '=' && prev.namedArg && opts.namedArgumentSpacing === 'none') return false;

  // separators and closers never take a space before them
  if (cp && (c === ',' || c === ';' || c === ':' || c === '.' || c === ')' || c === ']')) return false;
  if (cp && c === '}') {
    if (pp && p === '{') return false;
    return !!top && top.kind === 'block';
  }
  if (cp && c === '(') {
    if (prev.kind === 'ident') return CONTROL_KW.has(p);
    if (pp && (p === ')' || p === ']' || p === '(' || p === '[' || p === '.' || p === '@' || p === '!')) return false;
    if (pp && p === '{') return !!top && top.kind === 'block';
    if (pp && p === '>' && prev.generic) return false;
    if (pp && (p === '-' || p === '!') && prev.unary) return false;
    return true;
  }
  if (cp && c === '[') {
    if (prev.kind === 'ident' || prev.kind === 'string') return false;
    if (pp && (p === ')' || p === ']' || p === '(' || p === '[' || p === '.' || p === '!')) return false;
    if (pp && p === '{') return !!top && top.kind === 'block';
    if (pp && (p === '-' || p === '!') && prev.unary) return false;
    return true;
  }
  if (cp && c === '{') {
    if (braceKind === 'block') return !(pp && (p === '(' || p === '['));
    if (braceKind === 'init') return false;
    // map literal
    if (pp && (p === '(' || p === '[' || p === '{' || p === '.' || p === '!')) return false;
    return true;
  }

  // after openers / accessors
  if (pp && (p === '(' || p === '[' || p === '.' || p === '@' || p === '!')) return false;
  if (pp && p === '{') return !!top && top.kind === 'block';
  if (pp && p === '-' && prev.unary) return false;

  // generics
  if (pp && p === '<' && prev.generic) return false;
  if (pp && p === '>' && prev.generic) return cur.kind !== 'punct';
  if (cp && (c === '<' || c === '>') && cur.generic) return false;

  // binary operators
  if (cp && BINARY.has(c)) {
    if (c === '=' && cur.namedArg && opts.namedArgumentSpacing === 'none') return false;
    return true;
  }
  if (pp && BINARY.has(p)) {
    if (p === '=' && prev.namedArg && opts.namedArgumentSpacing === 'none') return false;
    return true;
  }
  return true;
}

function commentText(tok: FTok, text: string, lineMap: LineMap, level: number, indent: (l: number) => string, width: (s: string) => number): string {
  if (!tok.block || tok.endLine === tok.line) return tok.value.trimEnd();
  // Multi-line block comment: shift the inner lines by the same amount as the first line.
  const lineStart = lineMap.lineStart(tok.line);
  const original = text.slice(lineStart, tok.start);
  const delta = original.trim() === '' ? width(indent(level)) - width(original) : 0;
  const lines = tok.value.split('\n');
  const out = [lines[0].trimEnd()];
  for (let i = 1; i < lines.length; i++) {
    const raw = lines[i].replace(/\r$/, '');
    const lead = /^[ \t]*/.exec(raw)![0];
    const body = raw.slice(lead.length).trimEnd();
    if (!body) {
      out.push('');
      continue;
    }
    const w = Math.max(0, width(lead) + delta);
    out.push(' '.repeat(w) + body);
  }
  return out.join('\n');
}
