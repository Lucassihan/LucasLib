// Syntax highlighting + inlay-hint computation for the sandbox code editor.
//
// highlightSpans() returns contiguous, classified spans covering the WHOLE
// text (whitespace included), so the editor can rebuild the DOM char-for-char.
// inlayHints() reads the parse to place VS Code-style parameter-name hints
// before positional call arguments.

import { FN_BY_NAME } from './spec';
import type { ParseResult } from './parse';

export type TokClass =
  | 'comment'
  | 'number'
  | 'type' // BoomParams
  | 'fn' // movement / setPos call names
  | 'prop' // .field in a designated initializer
  | 'bool' // true / false
  | 'const' // NAN, msec, sec, chassis (namespace / builtins)
  | 'directive' // [TRAJECTORY] bracket marker
  | 'punct'
  | 'ident' // user variable (e.g. a BoomParams name)
  | 'ws'
  | 'text';

export interface HSpan {
  start: number;
  end: number;
  cls: TokClass;
}

const KNOWN_FNS = new Set(
  Object.keys(FN_BY_NAME).map((k) => (k.includes('.') ? k.split('.').pop()! : k))
);
const TYPES = new Set(['BoomParams', 'BezierSegment', 'vector', 'std']);
const BOOLS = new Set(['true', 'false']);
const CONSTS = new Set(['NAN', 'msec', 'sec', 'seconds', 'chassis', 'drivePID', 'turnPID', 'headingPID', 'totalOutput', 'backward']);

const isIdentStart = (c: string) => /[A-Za-z_]/.test(c);
const isIdent = (c: string) => /[A-Za-z0-9_]/.test(c);
const isDigit = (c: string) => c >= '0' && c <= '9';

export function highlightSpans(src: string): HSpan[] {
  const spans: HSpan[] = [];
  const n = src.length;
  let i = 0;
  let prevSignificant = ''; // last non-ws, non-comment char emitted

  const push = (start: number, end: number, cls: TokClass) => {
    if (end > start) spans.push({ start, end, cls });
  };

  while (i < n) {
    const c = src[i];

    // whitespace run
    if (c === ' ' || c === '\t' || c === '\r' || c === '\n') {
      const s = i;
      while (i < n && /\s/.test(src[i])) i++;
      push(s, i, 'ws');
      continue;
    }

    // line comment
    if (c === '/' && src[i + 1] === '/') {
      const s = i;
      while (i < n && src[i] !== '\n') i++;
      push(s, i, 'comment');
      continue;
    }
    // block comment
    if (c === '/' && src[i + 1] === '*') {
      const s = i;
      i += 2;
      while (i < n && !(src[i] === '*' && src[i + 1] === '/')) i++;
      i = Math.min(n, i + 2);
      push(s, i, 'comment');
      continue;
    }

    // number (allow leading dot, exponent, and a lone sign only after e/E)
    if (isDigit(c) || (c === '.' && isDigit(src[i + 1] ?? ''))) {
      const s = i;
      while (i < n && /[0-9.eE]/.test(src[i])) {
        if ((src[i] === '+' || src[i] === '-') && !/[eE]/.test(src[i - 1])) break;
        i++;
      }
      push(s, i, 'number');
      prevSignificant = ')';
      continue;
    }

    // identifier
    if (isIdentStart(c)) {
      const s = i;
      while (i < n && isIdent(src[i])) i++;
      const word = src.slice(s, i);
      // look ahead for '(' (a call) skipping spaces
      let j = i;
      while (j < n && /\s/.test(src[j])) j++;
      const followedByParen = src[j] === '(';
      let cls: TokClass;
      if (prevSignificant === '.') cls = 'prop';
      else if (TYPES.has(word)) cls = 'type';
      else if (BOOLS.has(word)) cls = 'bool';
      else if (CONSTS.has(word)) cls = 'const';
      else if (followedByParen || KNOWN_FNS.has(word)) cls = 'fn';
      else cls = 'ident';
      push(s, i, cls);
      prevSignificant = word;
      continue;
    }

    // [DIRECTIVE] bracket marker (e.g. [TRAJECTORY]) — colour the whole thing
    if (c === '[') {
      let j = i + 1;
      while (j < n && src[j] !== ']' && src[j] !== '\n') j++;
      if (src[j] === ']') {
        push(i, j + 1, 'directive');
        i = j + 1;
        prevSignificant = ']';
        continue;
      }
    }

    // punctuation / operators (single char)
    push(i, i + 1, 'punct');
    prevSignificant = c;
    i++;
  }

  return spans;
}

export interface InlayHint {
  offset: number; // char offset where the hint is inserted (before the arg)
  text: string; // e.g. 'x' — rendered as 'x:'
}

/** Parameter-name hints before each positional call argument. */
export function inlayHints(parsed: ParseResult): InlayHint[] {
  const hints: InlayHint[] = [];
  for (const s of parsed.stmts) {
    if (s.kind !== 'call') continue;
    const fn = FN_BY_NAME[s.fn];
    if (!fn) continue;
    const bpOverload = s.fn === 'facePoint' && s.args[0]?.kind === 'ident';
    s.args.forEach((a, i) => {
      const p = fn.params[i];
      if (!p) return;
      if (bpOverload && i === 0 && a.kind === 'ident') {
        hints.push({ offset: a.start, text: 'bp' });
        return;
      }
      // struct initializers are self-labeling; missing args have no literal
      if (a.kind === 'num') hints.push({ offset: a.lit.start, text: p.name });
      else if (a.kind === 'bool') hints.push({ offset: a.lit.start, text: p.name });
      else if (a.kind === 'ident') hints.push({ offset: a.start, text: p.name });
    });
  }
  return hints.sort((x, y) => x.offset - y.offset);
}
