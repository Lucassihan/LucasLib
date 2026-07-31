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

import { FN_BY_NAME, STRUCT_FIELDS, PID_FIELDS, TOTAL_FIELDS, type FnSpec } from './spec';

// Exit-condition fields the lib has renamed. Routes written before a rename still
// sit in saved autons, so say what to use instead of just "no such field".
const RENAMED_FIELDS: Record<string, string> = {
  extD: 'use .settleGate (the settle activation radius)',
  crossR: 'the ⊥ line is infinite now, so there is no length to set. Use .d alone',
  tOut: 'renamed to .t · -1 is the off sentinel now, not 0',
  setR: 'renamed to .settleGate (the settle activation zone)',
  setD: 'gone — settle is a SPEED test now, not a PID D term. Use .v (in/s on a lateral, rad/s on a turn)',
  crossD: 'renamed to .d',
  extA: 'renamed to .d · -1 is the off sentinel now, not 0',
  pLine: 'use .d, which slides the ⊥ line toward the robot instead of being a distance to it (-1 = off)'
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

/** A `[TRAJECTORY]` bracket directive. Its spline + follow settings live in the
 * sandbox store keyed by statement index — the token itself is just a marker. */
export interface TrajectoryStmt {
  kind: 'trajectory';
  name: string; // the bracket name, e.g. 'TRAJECTORY'
  line: number;
  start: number;
  end: number;
}

export type Stmt = CallStmt | BoomDeclStmt | AssignStmt | TrajectoryStmt;

export interface ParseError {
  line: number;
  message: string;
}

export interface ParseResult {
  stmts: Stmt[];
  errors: ParseError[];
  /** BoomParams declarations by name (last wins, like C++ shadowing would not — flag dupes) */
  booms: Record<string, BoomDeclStmt>;
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
    if ('(){}[],;=.-'.includes(c)) {
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

  while (p.peek()) {
    const t = p.peek()!;

    // [TRAJECTORY] directive — a bracket marker. The spline shape + follow
    // settings live in the sandbox store (keyed by stmt index), not the text.
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
      stmts.push({ kind: 'trajectory', name: nameTok.text, line: t.line, start: t.start, end: close.end });
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
        errors.push({ line: t.line, message: `Unknown function '${fnName}'` });
        p.sync();
        continue;
      }
      p.eat('(');
      const args: ArgValue[] = [];
      if (!p.at(')')) {
        for (;;) {
          const arg = parseArg(p, structKindAt(fn, args.length));
          if (!arg) {
            errors.push({ line: p.peek()?.line ?? t.line, message: `Bad argument ${args.length + 1} to ${fnName}` });
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
  // boomerangTo must reference a declared BoomParams (declared ABOVE the call)
  const declared = new Set<string>();
  for (const s of stmts) {
    if (s.kind === 'boomDecl') declared.add(s.name);
    if (s.kind === 'call' && (s.fn === 'boomerangTo' || s.fn === 'facePoint')) {
      const a0 = s.args[0];
      if (a0?.kind === 'ident' && !declared.has(a0.name) && a0.name !== 'chassis.h') {
        errors.push({ line: s.line, message: `'${a0.name}' is not a BoomParams declared above this line` });
      }
    }
    if (s.kind === 'call' && s.fn === 'boomerangTo' && s.args[0] && s.args[0].kind !== 'ident' && s.args[0].kind !== 'struct') {
      errors.push({ line: s.line, message: 'boomerangTo takes a BoomParams (declare one above and pass its name)' });
    }
  }

  return { stmts, errors, booms };
}

// ---------------------------------------------------------------------------
// Text-edit helpers for two-way sync (drag → rewrite literals in place).

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
