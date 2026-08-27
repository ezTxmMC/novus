/**
 * Tokenizer for the Novus language.
 *
 * Mirrors the token set of the C++ lexer in src/lexer and extends it with the
 * tokens needed for the concept syntax (comments, annotations, comparison and
 * logical operators, string interpolation).
 */

export enum TokenKind {
  Identifier = 'identifier',
  Integer = 'integer',
  Float = 'float',
  String = 'string',
  Punct = 'punct',
  EOF = 'eof',
}

export interface Span {
  start: number;
  end: number;
}

export interface StringPart {
  kind: 'text' | 'interp';
  value: string;
  start: number;
  end: number;
}

export interface Token extends Span {
  kind: TokenKind;
  value: string;
  /** True when at least one line break separates this token from the previous one. */
  newlineBefore: boolean;
  /** Only for string tokens: literal text chunks and `${...}` interpolations. */
  parts?: StringPart[];
}

export interface Comment extends Span {
  text: string;
  block: boolean;
}

export interface LexError extends Span {
  message: string;
}

export interface LexResult {
  tokens: Token[];
  comments: Comment[];
  errors: LexError[];
}

/** Words that can never be used as identifiers. */
export const RESERVED_WORDS = new Set<string>([
  'package', 'import', 'define', 'class', 'enum', 'interface', 'annotation', 'abstract',
  'method', 'construct', 'var', 'println', 'return', 'if', 'else', 'for', 'while', 'in',
  'based', 'this', 'true', 'false', 'null', 'private', 'public', 'protected', 'final',
  'static', 'break', 'continue',
]);

export const MODIFIER_WORDS = new Set<string>(['private', 'public', 'protected', 'final', 'static', 'abstract']);
export const DEFINE_KINDS = new Set<string>(['class', 'enum', 'interface', 'abstract', 'annotation']);

const TWO_CHAR_PUNCT = new Set(['==', '!=', '<=', '>=', '&&', '||']);
const ONE_CHAR_PUNCT = new Set('{}()[]<>,.:;=+-*/%!@?'.split(''));

function isIdentStart(c: string): boolean {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c === '_';
}

function isIdentPart(c: string): boolean {
  return isIdentStart(c) || (c >= '0' && c <= '9');
}

function isDigit(c: string): boolean {
  return c >= '0' && c <= '9';
}

const ESCAPES: Record<string, string> = { n: '\n', t: '\t', r: '\r', '"': '"', '\\': '\\', $: '$', '0': '\0' };

/**
 * Tokenizes `src`. `base` is added to every offset so that sub-lexing an
 * interpolation still yields absolute document offsets.
 */
export function lex(src: string, base = 0): LexResult {
  const tokens: Token[] = [];
  const comments: Comment[] = [];
  const errors: LexError[] = [];
  const n = src.length;
  let pos = 0;
  let newline = false;

  const push = (kind: TokenKind, value: string, start: number, end: number, parts?: StringPart[]): void => {
    tokens.push({ kind, value, start: start + base, end: end + base, newlineBefore: newline, parts });
    newline = false;
  };

  const readString = (): void => {
    const start = pos;
    pos++; // opening quote
    const parts: StringPart[] = [];
    let text = '';
    let textStart = pos;
    let closed = false;

    while (pos < n) {
      const c = src[pos];
      if (c === '"') {
        closed = true;
        break;
      }
      if (c === '\n') {
        // Strings end at the line break so a missing quote does not swallow the rest of the file.
        break;
      }
      if (c === '\\' && pos + 1 < n) {
        const e = src[pos + 1];
        text += ESCAPES[e] ?? e;
        pos += 2;
        continue;
      }
      if (c === '$' && src[pos + 1] === '{') {
        if (text.length) {
          parts.push({ kind: 'text', value: text, start: textStart + base, end: pos + base });
          text = '';
        }
        const exprStart = pos + 2;
        let depth = 1;
        let i = exprStart;
        while (i < n) {
          const ch = src[i];
          if (ch === '{') {
            depth++;
          } else if (ch === '}') {
            depth--;
            if (depth === 0) break;
          } else if (ch === '"') {
            // nested string inside the interpolation
            i++;
            while (i < n && src[i] !== '"') {
              if (src[i] === '\\') i++;
              i++;
            }
          }
          i++;
        }
        parts.push({ kind: 'interp', value: src.slice(exprStart, Math.min(i, n)), start: exprStart + base, end: Math.min(i, n) + base });
        if (i >= n) {
          errors.push({ message: 'Unterminated string interpolation', start: pos + base, end: n + base });
          pos = n;
          break;
        }
        pos = i + 1;
        textStart = pos;
        continue;
      }
      text += c;
      pos++;
    }

    if (text.length) {
      parts.push({ kind: 'text', value: text, start: textStart + base, end: pos + base });
    }
    if (closed) {
      pos++; // closing quote
    } else {
      errors.push({ message: 'Unterminated string literal', start: start + base, end: pos + base });
    }
    const value = parts.map(p => (p.kind === 'text' ? p.value : '${' + p.value + '}')).join('');
    push(TokenKind.String, value, start, pos, parts);
  };

  while (pos < n) {
    const c = src[pos];

    if (c === '\n') {
      newline = true;
      pos++;
      continue;
    }
    if (c === ' ' || c === '\t' || c === '\r' || c === '\f' || c === '\v') {
      pos++;
      continue;
    }

    if (c === '/' && src[pos + 1] === '/') {
      const start = pos;
      while (pos < n && src[pos] !== '\n') pos++;
      comments.push({ text: src.slice(start, pos), start: start + base, end: pos + base, block: false });
      continue;
    }
    if (c === '/' && src[pos + 1] === '*') {
      const start = pos;
      const close = src.indexOf('*/', pos + 2);
      if (close < 0) {
        errors.push({ message: 'Unterminated block comment', start: start + base, end: n + base });
        comments.push({ text: src.slice(start), start: start + base, end: n + base, block: true });
        pos = n;
      } else {
        pos = close + 2;
        comments.push({ text: src.slice(start, pos), start: start + base, end: pos + base, block: true });
      }
      continue;
    }

    if (isIdentStart(c)) {
      const start = pos;
      while (pos < n && isIdentPart(src[pos])) pos++;
      push(TokenKind.Identifier, src.slice(start, pos), start, pos);
      continue;
    }

    if (isDigit(c)) {
      const start = pos;
      while (pos < n && isDigit(src[pos])) pos++;
      let kind = TokenKind.Integer;
      if (src[pos] === '.' && isDigit(src[pos + 1] ?? '')) {
        kind = TokenKind.Float;
        pos++;
        while (pos < n && isDigit(src[pos])) pos++;
      }
      if (pos < n && isIdentStart(src[pos])) {
        const badStart = pos;
        while (pos < n && isIdentPart(src[pos])) pos++;
        errors.push({ message: `Invalid number literal '${src.slice(start, pos)}'`, start: badStart + base, end: pos + base });
      }
      push(kind, src.slice(start, pos), start, pos);
      continue;
    }

    if (c === '"') {
      readString();
      continue;
    }

    const two = src.substr(pos, 2);
    if (TWO_CHAR_PUNCT.has(two)) {
      push(TokenKind.Punct, two, pos, pos + 2);
      pos += 2;
      continue;
    }
    if (ONE_CHAR_PUNCT.has(c)) {
      push(TokenKind.Punct, c, pos, pos + 1);
      pos++;
      continue;
    }

    // Unknown character: group consecutive unknown characters into one error.
    const start = pos;
    const code = src.codePointAt(pos) ?? 0;
    pos += code > 0xffff ? 2 : 1;
    const ch = src.slice(start, pos);
    const message = ch === "'" ? "Unexpected ''' – Novus strings use double quotes" : `Unexpected character '${ch}'`;
    errors.push({ message, start: start + base, end: pos + base });
  }

  push(TokenKind.EOF, '', n, n);
  return { tokens, comments, errors };
}
