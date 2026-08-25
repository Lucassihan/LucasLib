// Parses sandbox route text — real LucasLib C++ calls — into typed statements.
//
// Supported statements:
//   chassis.setPos(x, y, h);
//   <movement>(args...);                    // signatures from spec.ts
//   BoomParams name {.x = 24, ...};         // designated initializers (= optional)
//   backward = true;                        // global drive-direction mode
//   drivePID.maxOutput = 10;                // PID config assignments
//   wait(900, msec);  /  wait(0.9, sec);
//   // line comments and /* block comments */
//
// Every numeric literal keeps its exact [start,end) character range so field
// drags can rewrite the text in place (two-way sync). Errors carry line
// numbers; parsing continues past them so one typo doesn't blank the field.

import { FN_BY_NAME, STRUCT_FIELDS, STRUCT_NAMES, PID_FIELDS, TOTAL_FIELDS, BEZIER_SEG, type FnSpec } from './spec';

// Exit-condition fields the lib has renamed. Routes written before a rename still
// sit in saved autons, so say what to use instead of just "no such field".
const RENAMED_FIELDS: Record<string, string> = {
  extD: 'use .settleWithin (the settle activation radius)',
  crossR: 'the ⊥ line is infinite now, so there is no length to set. Use .d alone',
  tOut: 'renamed to .t · -1 is the off sentinel now, not 0',
  setR: 'renamed to .settleWithin (the settle activation zone)',
  setD: 'gone — settle is a SPEED test now, not a PID D term. Use .v (in/s on a lateral, rad/s on a turn)',
  crossD: 'renamed to .d',
  extA: 'renamed to .d · -1 is the off sentinel now, not 0',
  pLine: 'use .d, which slides the ⊥ line toward the robot instead of being a distance to it (-1 = off)',
  settleGate: 'renamed to .settleWithin'
};

// Movements the lib has removed. Routes written against them still sit in saved
// autons, so say what replaced them. (An old route loaded through the store is
// rewritten automatically by migrateCode; this is for one typed by hand.)
const RENAMED_FUNCTIONS: Record<string, string> = {
  drive: 'use driveForward() or driveBackward(). Distance is a positive magnitude in both, and the direction is the function you call, not the sign',
  lyPose: 'the path-follow Lyapunov movement is gone — use ramsete.trajectory() with a std::vector<BezierSegment>',
  followPath: 'pure pursuit was retired for RAMSETE — use ramsete.trajectory() with a std::vector<BezierSegment>',
  followTrajectory: 'renamed to ramsete.trajectory()',
  executeTrajectory: 'renamed to ramsete.trajectory()',
  trajectory: 'it is a member of the ramsete object now, like moveTo — call ramsete.trajectory()'
};

export interface NumLit {
  value: number;
  start: number; // char offset in the source text
  end: number;
}

export interface BoolLit {
  value: boolean;
  start: number;
  end: number;
}

/** Brace initializer `{.a = 1, .b = true}` with per-field literals. */
export interface StructInit {
  start: number; // offset of '{'
  end: number; // offset just past '}'
  fields: Record<string, NumLit | BoolLit>;
}

export type ArgValue =
  | { kind: 'num'; lit: NumLit }
  | { kind: 'bool'; lit: BoolLit }
  | { kind: 'struct'; init: StructInit }
  | { kind: 'ident'; name: string; start: number; end: number } // BoomParams ref, msec, NAN, chassis.h
  | { kind: 'missing' }; // defaulted (not written)

export interface CallStmt {
  kind: 'call';
  fn: string; // 'moveTo', 'chassis.setPos', 'wait', ...
  args: ArgValue[];
  line: number;
  start: number;
  end: number;
}

export interface BoomDeclStmt {
  kind: 'boomDecl';
  name: string;
  init: StructInit;
  line: number;
  start: number;
  end: number;
}

/** `backward = true;` or `drivePID.maxOutput = 10;` style assignment. */
export interface AssignStmt {
  kind: 'assign';
  /** 'backward' | 'drivePID' | 'turnPID' */
  target: string;
  /** PID field name; '' for the bare backward global */
  prop: string;
  value: NumLit | BoolLit;
  line: number;
  start: number;
  end: number;
}

/** One control point of a BezierSegment: `{x, y}`, with the exact char range
 * of the whole `{…}` pair (not just the two numbers) so the field editor can
 * rewrite it as a unit. */
export interface PointLit {
  x: NumLit;
  y: NumLit;
  start: number;
  end: number;
}

/** One `{.a = {…}, .b = {…}, .reversed = true, …}` BezierSegment initializer. */
export interface SegInit {
  start: number;
  end: number;
  points: Partial<Record<'a' | 'b' | 'c' | 'd', PointLit>>;
  fields: Record<string, NumLit | BoolLit>; // reversed / stopEnd / maxVel / maxAccel
}

/** `std::vector<BezierSegment> name = {…};` (or the shorter `vector<BezierSegment>`). */
export interface PathDeclStmt {
  kind: 'pathDecl';
  name: string;
  segs: SegInit[];
  line: number;
  start: number;
  end: number;
}

export type Stmt = CallStmt | BoomDeclStmt | AssignStmt | PathDeclStmt;

export interface ParseError {
  line: number;
  message: string;
}

export interface ParseResult {
  stmts: Stmt[];
  errors: ParseError[];
  /** BoomParams declarations by name (last wins, like C++ shadowing would not — flag dupes) */
  booms: Record<string, BoomDeclStmt>;
  /** std::vector<BezierSegment> declarations by name (same shadowing rule as booms) */
  paths: Record<string, PathDeclStmt>;
}

// ---------------------------------------------------------------------------

interface Tok {
  kind: 'num' | 'ident' | 'punct';
  text: string;
  start: number;
  end: number;
  line: number;
}

function tokenize(src: string, errors: ParseError[]): Tok[] {
  const toks: Tok[] = [];
  let i = 0;
  let line = 1;
  const n = src.length;
  while (i < n) {
    const c = src[i];
    if (c === '\n') {
      line++;
      i++;
      continue;
    }
    if (c === ' ' || c === '\t' || c === '\r') {
      i++;
      continue;
    }
    // comments
    if (c === '/' && src[i + 1] === '/') {
      while (i < n && src[i] !== '\n') i++;
      continue;
    }
    if (c === '/' && src[i + 1] === '*') {
      i += 2;
      while (i < n && !(src[i] === '*' && src[i + 1] === '/')) {
        if (src[i] === '\n') line++;
        i++;
      }
      i += 2;
      continue;
    }
    // number (with optional leading -, handled at parse level for unary minus;
    // here parse digits/dot; a leading '.' number like .5 is valid C++)
    if (/[0-9]/.test(c) || (c === '.' && /[0-9]/.test(src[i + 1] ?? ''))) {
      const start = i;
      while (i < n && /[0-9.eE+-]/.test(src[i])) {
        // stop +/- unless right after e/E (exponent)
        if ((src[i] === '+' || src[i] === '-') && !/[eE]/.test(src[i - 1])) break;
        i++;
      }
      toks.push({ kind: 'num', text: src.slice(start, i), start, end: i, line });
      continue;
    }
    // identifier (allow dots for chassis.setPos / chassis.h)
    if (/[A-Za-z_]/.test(c)) {
      const start = i;
      while (i < n && /[A-Za-z0-9_]/.test(src[i])) i++;
      // dotted continuation: chassis.setPos, chassis.h
      while (src[i] === '.' && /[A-Za-z_]/.test(src[i + 1] ?? '')) {
        i++;
        while (i < n && /[A-Za-z0-9_]/.test(src[i])) i++;
      }
      toks.push({ kind: 'ident', text: src.slice(start, i), start, end: i, line });
      continue;
    }
    if ('(){}[],;=.-<>:'.includes(c)) {
      toks.push({ kind: 'punct', text: c, start: i, end: i + 1, line });
      i++;
      continue;
    }
    errors.push({ line, message: `Unexpected character '${c}'` });
    i++;
  }
  return toks;
}

// ---------------------------------------------------------------------------

class P {
  toks: Tok[];
  pos = 0;
  errors: ParseError[];
  constructor(toks: Tok[], errors: ParseError[]) {
    this.toks = toks;
    this.errors = errors;
  }
  peek(k = 0): Tok | undefined {
    return this.toks[this.pos + k];
  }
  next(): Tok | undefined {
    return this.toks[this.pos++];
  }
  at(text: string): boolean {
    const t = this.peek();
    return !!t && t.text === text;
  }
  eat(text: string): Tok | null {
    if (this.at(text)) return this.next()!;
    return null;
  }
  expect(text: string, what: string): Tok | null {
    const t = this.eat(text);
    if (!t) {
      const cur = this.peek();
      this.errors.push({
        line: cur?.line ?? this.toks[this.toks.length - 1]?.line ?? 1,
        message: `Expected '${text}' ${what}${cur ? `, got '${cur.text}'` : ''}`
      });
    }
    return t;
  }
  /** skip forward past the next ';' (error recovery) */
  sync() {
    while (this.pos < this.toks.length && !this.at(';')) this.pos++;
    this.eat(';');
  }
}

function parseNumber(p: P): NumLit | null {
  const neg = p.at('-') ? p.next()! : null;
  const t = p.peek();
  if (t && t.kind === 'num') {
    p.next();
    const value = Number((neg ? '-' : '') + t.text);
    if (Number.isNaN(value)) {
      p.errors.push({ line: t.line, message: `Bad number '${t.text}'` });
      return null;
    }
    return { value, start: neg ? neg.start : t.start, end: t.end };
  }
  if (neg) p.errors.push({ line: neg.line, message: `Expected a number after '-'` });
  return null;
}

function parseStructInit(p: P, structKind: string): StructInit | null {
  const open = p.expect('{', 'to open the initializer');
  if (!open) return null;
  const fields: Record<string, NumLit | BoolLit> = {};
  const specFields = STRUCT_FIELDS[structKind] ?? [];
  while (!p.at('}') && p.peek()) {
    if (!p.eat('.')) {
      p.errors.push({ line: p.peek()!.line, message: `Expected '.field = value' (designated initializer)` });
      break;
    }
    const nameTok = p.next();
    if (!nameTok || nameTok.kind !== 'ident') {
      p.errors.push({ line: nameTok?.line ?? open.line, message: 'Expected a field name after "."' });
      break;
    }
    const fs = specFields.find((f) => f.name === nameTok.text);
    if (!fs) {
      const gone = RENAMED_FIELDS[nameTok.text];
      p.errors.push({
        line: nameTok.line,
        message: gone
          ? `'.${nameTok.text}' was removed — ${gone}`
          : `No field '.${nameTok.text}' in this struct`
      });
    }
    p.expect('=', `after .${nameTok.text}`);
    const vt = p.peek();
    if (vt && vt.kind === 'ident' && (vt.text === 'true' || vt.text === 'false')) {
      p.next();
      fields[nameTok.text] = { value: vt.text === 'true', start: vt.start, end: vt.end };
    } else {
      const num = parseNumber(p);
      if (num) fields[nameTok.text] = num;
      else {
        p.errors.push({ line: nameTok.line, message: `Expected a value for .${nameTok.text}` });
        break;
      }
    }
    if (!p.eat(',')) break;
  }
  const close = p.expect('}', 'to close the initializer');
  return { start: open.start, end: (close ?? p.peek() ?? open).end, fields };
}

/** One control point `{x, y}` inside a BezierSegment initializer. */
function parsePointLit(p: P): PointLit | null {
  const open = p.expect('{', 'to open a point {x, y}');
  if (!open) return null;
  const x = parseNumber(p);
  if (!x) {
    p.errors.push({ line: open.line, message: 'Expected a number for the point’s x' });
    return null;
  }
  p.expect(',', 'between the point’s x and y');
  const y = parseNumber(p);
  if (!y) {
    p.errors.push({ line: open.line, message: 'Expected a number for the point’s y' });
    return null;
  }
  const close = p.expect('}', 'to close the point');
  return { x, y, start: open.start, end: (close ?? p.peek() ?? open).end };
}

const SEG_POINT_FIELDS = new Set(['a', 'b', 'c', 'd']);

/** One `{.a = {…}, .b = {…}, .c = {…}, .d = {…}, .reversed = true, …}` segment. */
function parseSegInit(p: P): SegInit | null {
  const open = p.expect('{', 'to open the segment initializer');
  if (!open) return null;
  const points: SegInit['points'] = {};
  const fields: SegInit['fields'] = {};
  while (!p.at('}') && p.peek()) {
    if (!p.eat('.')) {
      p.errors.push({ line: p.peek()!.line, message: `Expected '.field = value' (designated initializer)` });
      break;
    }
    const nameTok = p.next();
    if (!nameTok || nameTok.kind !== 'ident') {
      p.errors.push({ line: nameTok?.line ?? open.line, message: 'Expected a field name after "."' });
      break;
    }
    const fs = BEZIER_SEG.find((f) => f.name === nameTok.text);
    if (!fs) {
      const gone = RENAMED_FIELDS[nameTok.text];
      p.errors.push({
        line: nameTok.line,
        message: gone
          ? `'.${nameTok.text}' was removed — ${gone}`
          : `No field '.${nameTok.text}' in BezierSegment`
      });
    }
    p.expect('=', `after .${nameTok.text}`);
    if (SEG_POINT_FIELDS.has(nameTok.text)) {
      const pt = parsePointLit(p);
      if (pt) points[nameTok.text as 'a' | 'b' | 'c' | 'd'] = pt;
      else {
        p.errors.push({ line: nameTok.line, message: `Expected a {x, y} point for .${nameTok.text}` });
        break;
      }
    } else {
      const vt = p.peek();
      if (vt && vt.kind === 'ident' && (vt.text === 'true' || vt.text === 'false')) {
        p.next();
        fields[nameTok.text] = { value: vt.text === 'true', start: vt.start, end: vt.end };
      } else {
        const num = parseNumber(p);
        if (num) fields[nameTok.text] = num;
        else {
          p.errors.push({ line: nameTok.line, message: `Expected a value for .${nameTok.text}` });
          break;
        }
      }
    }
    if (!p.eat(',')) break;
  }
  const close = p.expect('}', 'to close the segment initializer');
  return { start: open.start, end: (close ?? p.peek() ?? open).end, points, fields };
}

/**
 * `std::vector<BezierSegment> name = {…};` — the `std::` prefix and the `=`
 * are both optional (direct-list-init is the house style, same allowance
 * BoomParams gets). Called with the current token already confirmed to be
 * `std` or `vector`.
 */
function parsePathDecl(p: P): PathDeclStmt | null {
  const startTok = p.peek()!;
  if (p.at('std')) {
    p.next();
    p.expect(':', 'after std');
    p.expect(':', 'after std:');
  }
  if (!p.expect('vector', 'to declare a path')) {
    p.sync();
    return null;
  }
  p.expect('<', 'after vector');
  const segTok = p.next();
  if (!segTok || segTok.text !== 'BezierSegment') {
    p.errors.push({ line: segTok?.line ?? startTok.line, message: `Expected 'BezierSegment' inside vector<...>` });
    p.sync();
    return null;
  }
  p.expect('>', 'to close vector<BezierSegment>');
  const nameTok = p.next();
  if (!nameTok || nameTok.kind !== 'ident') {
    p.errors.push({ line: nameTok?.line ?? startTok.line, message: 'Expected a name after vector<BezierSegment>' });
    p.sync();
    return null;
  }
  p.eat('='); // optional: direct-list-init `path {…}` is the house style
  const open = p.expect('{', 'to open the path');
  if (!open) {
    p.sync();
    return null;
  }
  const segs: SegInit[] = [];
  while (!p.at('}') && p.peek()) {
    const seg = parseSegInit(p);
    if (!seg) break;
    segs.push(seg);
    if (!p.eat(',')) break;
  }
  const close = p.expect('}', 'to close the path');
  const semi = p.expect(';', 'after the declaration');
  return {
    kind: 'pathDecl',
    name: nameTok.text,
    segs,
    line: startTok.line,
    start: startTok.start,
    end: (semi ?? close ?? nameTok).end
  };
}

function parseArg(p: P, structKind?: string): ArgValue | null {
  const t = p.peek();
  if (!t) return null;
  if (t.text === '{') {
    const init = parseStructInit(p, structKind ?? 'lateralEc');
    return init ? { kind: 'struct', init } : null;
  }
  if (t.kind === 'ident') {
    if (t.text === 'true' || t.text === 'false') {
      p.next();
      return { kind: 'bool', lit: { value: t.text === 'true', start: t.start, end: t.end } };
    }
    p.next();
    return { kind: 'ident', name: t.text, start: t.start, end: t.end };
  }
  const num = parseNumber(p);
  if (num) return { kind: 'num', lit: num };
  return null;
}

/** Which struct kind the i-th parameter of `fn` expects (for brace args). */
function structKindAt(fn: FnSpec | undefined, i: number): string | undefined {
  const k = fn?.params[i]?.kind;
  return k === 'lateralEc' || k === 'turnEc' || k === 'blend' || k === 'boomParams' ? k : undefined;
}

export function parseRoute(src: string): ParseResult {
  const errors: ParseError[] = [];
  const toks = tokenize(src, errors);
  const p = new P(toks, errors);
  const stmts: Stmt[] = [];
  const booms: Record<string, BoomDeclStmt> = {};
  const paths: Record<string, PathDeclStmt> = {};

  while (p.peek()) {
    const t = p.peek()!;

    // [NAME] bracket — the old [TRAJECTORY] sandbox marker is gone; a real
    // path is a std::vector<BezierSegment> declaration + ramsete.trajectory()
    // call now. Still consume the bracket (so stale routes don't cascade into
    // unrelated parse errors) and point at the replacement.
    if (t.kind === 'punct' && t.text === '[') {
      p.next();
      const nameTok = p.next();
      const close = p.next();
      if (!nameTok || nameTok.kind !== 'ident' || !close || close.text !== ']') {
        errors.push({ line: t.line, message: 'Expected a [NAME] directive like [TRAJECTORY]' });
        p.sync();
        continue;
      }
      p.eat(';'); // optional trailing semicolon
      errors.push({
        line: t.line,
        message: `[${nameTok.text}] is gone — declare a std::vector<BezierSegment> and call ramsete.trajectory(path)`
      });
      continue;
    }

    // Path decl — `std::vector<BezierSegment> name = {…};` or the shorter
    // `vector<BezierSegment> name = {…};`.
    if (t.kind === 'ident' && (t.text === 'std' || t.text === 'vector')) {
      const stmt = parsePathDecl(p);
      if (stmt) {
        if (paths[stmt.name]) errors.push({ line: stmt.line, message: `Path '${stmt.name}' is declared twice` });
        stmts.push(stmt);
        paths[stmt.name] = stmt;
      }
      continue;
    }

    // BoomParams decl — canonical form `BoomParams bp {…};`; `=` also accepted
    if (t.kind === 'ident' && t.text === 'BoomParams') {
      p.next();
      const nameTok = p.next();
      if (!nameTok || nameTok.kind !== 'ident') {
        errors.push({ line: t.line, message: 'Expected a name after BoomParams' });
        p.sync();
        continue;
      }
      p.eat('='); // optional: direct-list-init `bp {…}` is the house style
      const init = parseStructInit(p, 'boomParams');
      const semi = p.expect(';', 'after the declaration');
      if (init) {
        const stmt: BoomDeclStmt = {
          kind: 'boomDecl',
          name: nameTok.text,
          init,
          line: t.line,
          start: t.start,
          end: (semi ?? nameTok).end
        };
        if (booms[stmt.name]) errors.push({ line: t.line, message: `BoomParams '${stmt.name}' is declared twice` });
        stmts.push(stmt);
        booms[stmt.name] = stmt;
      } else {
        p.sync();
      }
      continue;
    }

    // assignment statement: `backward = <bool>;` or `drivePID.<field> = <num>;`
    if (
      t.kind === 'ident' &&
      (t.text === 'backward' ||
        t.text.startsWith('drivePID.') ||
        t.text.startsWith('turnPID.') ||
        t.text.startsWith('headingPID.') ||
        t.text.startsWith('totalOutput.'))
    ) {
      p.next();
      let target = t.text;
      let prop = '';
      if (t.text !== 'backward') {
        const dot = t.text.indexOf('.');
        target = t.text.slice(0, dot);
        prop = t.text.slice(dot + 1);
        const fields = target === 'totalOutput' ? TOTAL_FIELDS : PID_FIELDS;
        if (!fields.some((f) => f.name === prop)) {
          errors.push({ line: t.line, message: `${target} has no assignable field '${prop}'` });
        }
      }
      p.expect('=', `after ${t.text}`);
      let value: NumLit | BoolLit | null = null;
      const vt = p.peek();
      if (vt && vt.kind === 'ident' && (vt.text === 'true' || vt.text === 'false')) {
        p.next();
        value = { value: vt.text === 'true', start: vt.start, end: vt.end };
      } else {
        value = parseNumber(p);
      }
      const semi = p.expect(';', `after the assignment`);
      if (value === null) {
        errors.push({ line: t.line, message: `Expected a value for ${t.text}` });
        p.sync();
        continue;
      }
      if (t.text === 'backward' && typeof value.value !== 'boolean') {
        errors.push({ line: t.line, message: 'backward takes true or false' });
      }
      if (t.text !== 'backward' && typeof value.value !== 'number') {
        errors.push({ line: t.line, message: `${t.text} takes a number` });
      }
      stmts.push({ kind: 'assign', target, prop, value, line: t.line, start: t.start, end: (semi ?? t).end });
      continue;
    }

    // call statement
    if (t.kind === 'ident') {
      const fnName = t.text;
      const fn = FN_BY_NAME[fnName];
      p.next();
      if (!p.at('(')) {
        errors.push({ line: t.line, message: `Unknown statement '${fnName}' — expected a movement call` });
        p.sync();
        continue;
      }
      if (!fn) {
        const gone = RENAMED_FUNCTIONS[fnName];
        errors.push({
          line: t.line,
          message: gone ? `'${fnName}' was removed — ${gone}` : `Unknown function '${fnName}'`
        });
        p.sync();
        continue;
      }
      p.eat('(');
      const args: ArgValue[] = [];
      if (!p.at(')')) {
        for (;;) {
          const at = args.length;
          // A brace initializer in a slot that isn't struct-typed. C++ would
          // refuse to convert {...} to a double, but this parser used to accept
          // it silently and leave it sitting in the wrong argument — so
          // `driveBackward(9, {.v = 10})` put the exit conditions in the HEADING
          // slot, the sim read argument 3, found nothing, and used defaults.
          // The movement then ignored every exit condition written on it, which
          // reads as "changing .v does nothing".
          if (p.at('{') && !structKindAt(fn, at)) {
            const here = fn.params[at];
            const want = fn.params.findIndex((q) => STRUCT_NAMES[q.kind]);
            if (!here) {
              errors.push({ line: p.peek()!.line, message: `${fnName} takes at most ${fn.params.length} arguments` });
            } else {
              const type = here.kind === 'int' ? 'int' : here.kind === 'bool' ? 'bool' : 'double';
              const parts = [`Argument ${at + 1} of ${fnName} is \`${type} ${here.name}\`, not an initializer`];
              if (want > at) {
                const struct = STRUCT_NAMES[fn.params[want].kind];
                parts.push(
                  here.def
                    ? `pass its default (${here.def}) here and put ${struct} in argument ${want + 1}`
                    : `${struct} is argument ${want + 1}`
                );
              }
              errors.push({ line: p.peek()!.line, message: parts.join(' — ') });
            }
          }
          const arg = parseArg(p, structKindAt(fn, at));
          if (!arg) {
            errors.push({ line: p.peek()?.line ?? t.line, message: `Bad argument ${at + 1} to ${fnName}` });
            break;
          }
          args.push(arg);
          if (!p.eat(',')) break;
        }
      }
      p.expect(')', `to close ${fnName}(...)`);
      const semi = p.expect(';', `after ${fnName}(...)`);
      if (args.length > fn.params.length) {
        errors.push({ line: t.line, message: `${fnName} takes at most ${fn.params.length} arguments` });
      }
      // required args present? (facePoint(bp) overload passes one BoomParams)
      const bpOverload = fnName === 'facePoint' && args[0]?.kind === 'ident';
      const required = fn.params.filter((q) => q.def === undefined).length;
      if (args.length < required && !bpOverload) {
        errors.push({ line: t.line, message: `${fnName} needs at least ${required} argument${required === 1 ? '' : 's'}` });
      }
      stmts.push({ kind: 'call', fn: fnName, args, line: t.line, start: t.start, end: (semi ?? t).end });
      continue;
    }

    errors.push({ line: t.line, message: `Unexpected '${t.text}'` });
    p.sync();
  }

  // route rules
  const firstCall = stmts.find((s) => s.kind === 'call');
  if (stmts.length && (!firstCall || (firstCall as CallStmt).fn !== 'chassis.setPos')) {
    errors.push({ line: 1, message: 'Every auton must start with chassis.setPos(x, y, h);' });
  }
  // boomerangTo must reference a declared BoomParams (declared ABOVE the call);
  // trajectory must reference a declared path the same way.
  const declared = new Set<string>();
  const declaredPaths = new Set<string>();
  for (const s of stmts) {
    if (s.kind === 'boomDecl') declared.add(s.name);
    if (s.kind === 'pathDecl') declaredPaths.add(s.name);
    if (s.kind === 'call' && (s.fn === 'boomerangTo' || s.fn === 'facePoint')) {
      const a0 = s.args[0];
      if (a0?.kind === 'ident' && !declared.has(a0.name) && a0.name !== 'chassis.h') {
        errors.push({ line: s.line, message: `'${a0.name}' is not a BoomParams declared above this line` });
      }
    }
    if (s.kind === 'call' && s.fn === 'boomerangTo' && s.args[0] && s.args[0].kind !== 'ident' && s.args[0].kind !== 'struct') {
      errors.push({ line: s.line, message: 'boomerangTo takes a BoomParams (declare one above and pass its name)' });
    }
    if (s.kind === 'call' && s.fn === 'ramsete.trajectory') {
      const a0 = s.args[0];
      if (a0?.kind === 'ident' && !declaredPaths.has(a0.name)) {
        errors.push({ line: s.line, message: `'${a0.name}' is not a std::vector<BezierSegment> declared above this line` });
      }
    }
  }

  // Path shape: every segment needs all four control points, a path needs at
  // least one segment, and consecutive segments must share an endpoint — the
  // bake has no notion of a "gap" between segments, it just walks the array.
  for (const s of stmts) {
    if (s.kind !== 'pathDecl') continue;
    if (!s.segs.length) {
      errors.push({ line: s.line, message: 'A path needs at least one segment' });
      continue;
    }
    for (const seg of s.segs) {
      if (!seg.points.a || !seg.points.b || !seg.points.c || !seg.points.d) {
        errors.push({ line: s.line, message: 'Each BezierSegment needs .a, .b, .c and .d' });
      }
    }
    for (let i = 0; i + 1 < s.segs.length; i++) {
      const d = s.segs[i].points.d;
      const a = s.segs[i + 1].points.a;
      if (!d || !a) continue; // already flagged as incomplete above
      if (Math.abs(d.x.value - a.x.value) > 0.05 || Math.abs(d.y.value - a.y.value) > 0.05) {
        errors.push({
          line: s.line,
          message: `Segment ${i + 1} ends at (${d.x.value}, ${d.y.value}) but segment ${i + 2} starts at (${a.x.value}, ${a.y.value}) — a path must be continuous`
        });
      }
    }
  }

  return { stmts, errors, booms, paths };
}

// ---------------------------------------------------------------------------
// Text-edit helpers for two-way sync (drag → rewrite literals in place).

/**
 * Rewrite route text written against a since-removed API. Routes are the user's
 * own work and live both in localStorage and in `sandbox autons/*.txt`, so a
 * library rename cannot just start reporting "unknown function" at them — the
 * code has to come forward on its own. Called by the store on every load.
 *
 * Currently one rewrite: the signed-distance `drive()` became the
 * `driveForward` / `driveBackward` pair, where distance is a positive magnitude
 * and the sign moved into the function name, so `drive(-9)` is
 * `driveBackward(9)`. A `drive()` whose first argument is not a plain literal —
 * a variable, an expression — is left alone, because guessing its sign would be
 * worse than the error message RENAMED_FUNCTIONS gives it.
 */
export function migrateCode(code: string): string {
  // The lookbehind keeps `voldrive(` and `driveForward(` out of it.
  const drives = code.replace(
    /(?<![A-Za-z0-9_])drive\(\s*(-)?\s*(\d*\.?\d+)/g,
    (_m, neg: string | undefined, num: string) => `${neg ? 'driveBackward' : 'driveForward'}(${num}`
  );

  // The movement has been renamed twice: executeTrajectory() → trajectory() →
  // ramsete.trajectory(). Saved autons still call the older names, and there is
  // nothing ambiguous about any of them — so rewrite rather than make the user
  // do it. The '.' in the lookbehind is what stops an already-migrated
  // ramsete.trajectory( from picking up a second prefix; the trailing '(' is
  // what keeps createTrajectory/trajectoryDuration out of it.
  return drives.replace(
    /(?<![A-Za-z0-9_.])(?:executeTrajectory|followTrajectory|trajectory)(?=\s*\()/g,
    'ramsete.trajectory'
  );
}

export function fmtNum(v: number, decimals = 1): string {
  const s = v.toFixed(decimals);
  return s.replace(/\.?0+$/, '') || '0';
}

/** Replace one literal's characters with a new number. Returns new source. */
export function rewriteNumber(src: string, lit: { start: number; end: number }, value: number, decimals = 1): string {
  return src.slice(0, lit.start) + fmtNum(value, decimals) + src.slice(lit.end);
}

/**
 * Set a designated-initializer field: rewrite it if present, else insert
 * `.name = value` (with `, ` as needed) before the closing brace.
 */
export function setStructField(
  src: string,
  init: StructInit,
  name: string,
  value: number,
  decimals = 1
): string {
  const existing = init.fields[name];
  if (existing && typeof existing.value === 'number') {
    return rewriteNumber(src, existing, value, decimals);
  }
  const closeIdx = src.lastIndexOf('}', init.end - 1);
  const inner = src.slice(init.start + 1, closeIdx).trim();
  const insert = `.${name} = ${fmtNum(value, decimals)}`;
  const body = inner.length ? `${src.slice(init.start + 1, closeIdx).replace(/\s*$/, '')}, ${insert}` : insert;
  return src.slice(0, init.start + 1) + body + src.slice(closeIdx);
}
