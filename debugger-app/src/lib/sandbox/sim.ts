// Sandbox trajectory simulator — a deterministic (noise-free) port of the
// unicycle model used by scripts/gen-sample-trace.mjs, chained across the
// parsed route: each movement starts where the previous one ended, seeded by
// chassis.setPos.
//
// Real code parameters (targets, exit conditions, headings, BoomParams,
// `backward = …;` and `drivePID.… = …;` assignments) come from the text.
// Anything the sandbox has to invent about real-world behavior (top speed,
// weight, heading-correction effort) comes from SimKnobs — the left panel.

import type { ParseResult, CallStmt, ArgValue, StructInit } from './parse';
import { STRUCT_FIELDS } from './spec';
import { bakeTrajectory, defaultTrajectory, type TrajectoryMap } from '$lib/trajectory/spline';

export interface SimKnobs {
  // Drivetrain spec — the robot's motion limits are DERIVED from these via
  // deriveMotion(): 6 × 11 W V5 motors, wheel rpm + diameter set top speed and
  // force, weight (2–10 lb, base 2) sets accel, trackwidth sets the angular numbers.
  /** output rpm at the wheels (cartridge × external gearing) */
  wheelRpm: number;
  /** wheel diameter, inches */
  wheelDiameter: number;
  /** robot weight, lb — 2 to 10, base (floor) 2 lb */
  robotWeight: number;
  // PID proportional gains — how AGGRESSIVE each channel is. Same shape as the
  // real controllers; the sim scales output volts from these.
  /** drive P gain, V per inch of error */
  driveKp: number;
  /** turn P gain, V per deg — approach aggression (still under the no-overshoot brake) */
  turnKp: number;
  /** swing P gain, V per deg — swing approach aggression, separate from turns */
  swingKp: number;
  /** heading-correction P gain, V per deg (laterals; × the per-movement effort) */
  headingKp: number;
  // Motion chaining — how much momentum survives an EARLY EXIT into the next
  // movement (global sliders, not per-movement). 0 = clean handoffs, 1 = full carry.
  /** leftover drive speed slides/arcs through a following turn */
  lateralMomentum: number;
  /** leftover spin from a turn swings through the start of a following lateral */
  angularMomentum: number;
  /** omni wheels barely resist the lateral slide — carry is far more pronounced */
  omniWheels: boolean;
  /** default heading-correction effort for moveTo (per-movement overrides exist) */
  moveToHeadingEffort: number;
  /** default heading-correction effort for drive() */
  driveHeadingEffort: number;
  /** library global: within this dist moveTo stops correcting heading */
  headingDisableDist: number;
  /** chassis trackwidth, in — derived from robot settings (width − 2), not a knob */
  trackwidth: number;

  // Default exit conditions — what an OMITTED field of a LateralExitConditions /
  // TurnExitConditions initializer resolves to (mirrors exitConditions.h, but
  // tunable in the sandbox). Values written explicitly in the code still win.
  ecLatT: number;
  ecLatV: number;
  ecLatD: number;
  ecLatGate: number;
  ecTurnT: number;
  ecTurnV: number;
  ecTurnD: number;
  ecTurnGate: number;
}

export const DEFAULT_KNOBS: SimKnobs = {
  wheelRpm: 450,
  wheelDiameter: 3.25,
  robotWeight: 6,
  driveKp: 0.45,
  turnKp: 0.5,
  swingKp: 0.5,
  headingKp: 1,
  lateralMomentum: 0.35,
  angularMomentum: 0.35,
  omniWheels: true,
  moveToHeadingEffort: 0.9,
  driveHeadingEffort: 0.8,
  headingDisableDist: 6,
  trackwidth: 13,
  // mirror exitConditions.h defaults
  ecLatT: 5000,
  ecLatV: 10,
  ecLatD: -1,
  ecLatGate: 12,
  ecTurnT: 5000,
  ecTurnV: 10,
  ecTurnD: -1,
  ecTurnGate: 20
};

/** Per-movement heading-correction-effort overrides, keyed by statement index. */
export type EffortOverrides = Record<number, number>;

/**
 * Physical motion limits derived from the drivetrain spec: 6 × 11 W V5 motors.
 * A V5 motor's stall torque × cartridge rpm ≈ constant (2.1 N·m · 100 rpm), so
 * at output rpm R each motor delivers ≈ 210/R N·m at the wheel.
 *  - top linear speed: free speed × 0.9 load factor
 *  - top angular speed: differential-drive geometry (±v at ±trackwidth/2), × 0.8 scrub
 *  - linear accel: min(torque-limited F/m, traction-limited μ·g)
 *  - angular accel: the same force couple about the center, box inertia with a
 *    1.5× mass-spread factor (I = m·W²/4)
 */
export function deriveMotion(knobs: SimKnobs): {
  topLinear: number; // in/s
  topAngular: number; // deg/s
  linAccel: number; // in/s²
  angAccel: number; // deg/s²
} {
  const rpm = Math.max(50, knobs.wheelRpm);
  const dia = Math.max(1.5, knobs.wheelDiameter);
  const massKg = Math.max(2, knobs.robotWeight) * 0.4536; // base weight 2 lb
  const track = Math.max(6, knobs.trackwidth);
  const trackM = track * 0.0254;
  const mu = knobs.omniWheels ? 0.95 : 1.1; // wheel↔foam friction
  const G = 9.81;
  const topLinear = (rpm / 60) * Math.PI * dia * 0.9;
  const topAngular = ((2 * topLinear) / track) * R2D * 0.8;
  const F = (6 * (210 / rpm)) / ((dia / 2) * 0.0254); // N at the wheels
  const linAccel = Math.min(F / massKg, mu * G) * 39.37;
  const I = (massKg * trackM * trackM) / 4;
  const angAccel = ((Math.min(F, mu * massKg * G) * (trackM / 2)) / I) * R2D;
  return { topLinear, topAngular, linAccel, angAccel };
}

export interface Pose {
  x: number;
  y: number;
  h: number;
}

export interface SimTick {
  t: number; // ms
  x: number;
  y: number;
  h: number;
  // per-tick controller stats (for the PID view) — volts are post-governor
  /** lateral / drive error, in */
  de?: number;
  /** heading error, deg */
  he?: number;
  /** drive output, V */
  dp?: number;
  /** turn/heading output, V */
  tp?: number;
  // Synthesized P/I/D term breakdown (clean, no noise — for the replay-style
  // breakdown graphs). P = kp·e; I/D from plausible ki/kd ratios of kp.
  dP?: number;
  dI?: number;
  dD?: number;
  tP?: number;
  tI?: number;
  tD?: number;
  // live boomerang geometry (boomerang segments only) — feeds the replay
  // BoomerangOverlay: retracting carrot, trailing ghost, active stage, turn arc
  carrotX?: number;
  carrotY?: number;
  ghostX?: number;
  ghostY?: number;
  /** 0 = chasing ghost · 1 = carrot · 2 = end latched */
  stage?: number;
  /** instantaneous travel-arc radius, in */
  arcR?: number;
}

export interface BoomOverlay {
  target: { x: number; y: number };
  heading: number;
  lead: number;
  gLead: number;
  backward: boolean; // global mode at the time of the call
  /** carrot at max lead (s = 1) — the display position */
  initCarrot: { x: number; y: number };
  carrotTrack: { x: number; y: number }[];
  ghostTrack: { x: number; y: number }[];
  /** the ec's perp-line exit distance (0 = off) — for the overlay's exit plane */
  perpLine: number;
}

export interface SimSegment {
  stmtIndex: number;
  fn: string;
  ticks: SimTick[];
  start: Pose;
  end: Pose;
  exit: string;
  /** global backward mode this movement ran under */
  backward: boolean;
  boom?: BoomOverlay;
  /** governor configs in effect when this movement started (for the PID view) */
  pids?: PidSnapshot;
}

export interface PidSnapshot {
  drive: PidCfg;
  turn: PidCfg;
  heading: PidCfg;
  total: TotalCfg;
}

export interface SimResult {
  segments: SimSegment[];
  duration: number; // ms
  startPose: Pose | null;
  endPose: Pose | null;
  /** route-level problem that stopped simulation (missing setPos etc.) */
  error?: string;
}

const TICK = 10;
const DT = TICK / 1000;
const D2R = Math.PI / 180;
const R2D = 180 / Math.PI;
const clamp = (v: number, m: number) => Math.max(-m, Math.min(m, v));
const wrap = (a: number) => {
  while (a > 180) a -= 360;
  while (a < -180) a += 360;
  return a;
};

// --- argument evaluation ----------------------------------------------------

function argNum(a: ArgValue | undefined, def: number): number {
  if (!a) return def;
  if (a.kind === 'num') return a.lit.value;
  return def;
}
function structVals(
  kind: string,
  a: ArgValue | undefined,
  defaults?: Record<string, number>
): Record<string, number | boolean> {
  const out: Record<string, number | boolean> = {};
  for (const f of STRUCT_FIELDS[kind] ?? []) out[f.name] = defaults?.[f.name] ?? f.def;
  if (a?.kind === 'struct') {
    for (const [k, lit] of Object.entries(a.init.fields)) out[k] = lit.value;
  }
  return out;
}
function boomVals(init: StructInit): Record<string, number | boolean> {
  const out: Record<string, number | boolean> = {};
  for (const f of STRUCT_FIELDS.boomParams) out[f.name] = f.def;
  for (const [k, lit] of Object.entries(init.fields)) out[k] = lit.value;
  return out;
}

// --- exit-condition + PID-config shapes ---------------------------------------

interface LatEc {
  t: number;
  v: number;
  d: number;
  settleGate: number;
}
interface TurnEc {
  t: number;
  v: number;
  d: number;
  settleGate: number;
}

/** Assignable PID channel config (mirrors the LucasLib PID public fields). */
export interface PidCfg {
  targetOffset: number;
  maxOutput: number;
  minOutput: number;
  maxAccelRate: number;
  maxDecelRate: number;
}
const defaultPid = (): PidCfg => ({
  targetOffset: 0,
  maxOutput: 12,
  minOutput: 0,
  maxAccelRate: 0.6,
  maxDecelRate: 1.2
});

/** Assignable stage-2 governor (mirrors LucasLib TotalOutput — per-SIDE volts). */
export interface TotalCfg {
  minOutput: number;
  maxOutput: number;
  maxAccelRate: number;
  maxDecelRate: number;
}
const defaultTotal = (): TotalCfg => ({ minOutput: 0, maxOutput: 12, maxAccelRate: 0, maxDecelRate: 0 });

const HARD_CAP_MS = 15000; // sim safety net per movement

// ---------------------------------------------------------------------------

export function simulate(
  parsed: ParseResult,
  knobs: SimKnobs,
  efforts: EffortOverrides = {},
  trajectories: TrajectoryMap = {}
): SimResult {
  const segments: SimSegment[] = [];
  let t = 0;

  const first = parsed.stmts.find((s) => s.kind === 'call') as CallStmt | undefined;
  if (!first || first.fn !== 'chassis.setPos') {
    return { segments, duration: 0, startPose: null, endPose: null, error: 'Route must start with chassis.setPos(x, y, h);' };
  }

  const pose: Pose = {
    x: argNum(first.args[0], 0),
    y: argNum(first.args[1], 0),
    h: argNum(first.args[2], 0)
  };
  const startPose = { ...pose };

  // Global drive direction — REAL LucasLib global: initializes FALSE and nothing
  // ever resets it. Point/pose movements honor it; face/swings-to-heading and
  // drive() don't — drive() takes its direction from the sign of distance.
  let gBack = false;

  // Assignable PID channel configs. drivePID governs forward output; turnPID
  // governs actual turns (face/swing); headingPID governs the heading-correction
  // output of LATERAL movements (moveTo / drive / boomerang). Persist
  // across movements except targetOffset, which each movement consumes then zeroes.
  const pid = { drive: defaultPid(), turn: defaultPid(), heading: defaultPid() };
  // Stage-2 governor (LucasLib TotalOutput): slew + ratio-preserving cap + floor
  // on the PER-SIDE (left/right) voltages, applied after the channel PIDs blend.
  const tout = defaultTotal();
  type Chan = 'drive' | 'turn' | 'heading';
  function takeOffset(chan: Chan): number {
    const v = pid[chan].targetOffset;
    pid[chan].targetOffset = 0;
    return v;
  }

  let lastArcRadius = 1e4;
  let pendingVol: { left: number; right: number } | null = null;

  // Motion limits derived from the drivetrain spec (6 × 11 W, wheel rpm/diameter,
  // weight, trackwidth) — see deriveMotion().
  const motion = deriveMotion(knobs);
  const maxSpeedEff = Math.max(1, motion.topLinear); // in/s at full drive
  const maxWEff = Math.max(1, motion.topAngular); // deg/s at full turn
  const latAccel = Math.max(1, motion.linAccel); // in/s²
  const angAccel = Math.max(1, motion.angAccel); // deg/s²
  const vel = { v: 0, w: 0 }; // velocity state (momentum carries between movements)

  // Motion chaining (left-panel sliders): how much momentum survives an early
  // exit into the next movement.
  const latMom = Math.max(0, Math.min(1, knobs.lateralMomentum));
  const angMom = Math.max(0, Math.min(1, knobs.angularMomentum));
  const omni = !!knobs.omniWheels;

  /**
   * Linear-momentum carry into a turn: entered with leftover drive speed (an
   * early-exited lateral), the robot keeps translating for a split second while
   * it spins. Grip wheels arc the carry along the rotating heading and shed it
   * at full braking; omnis barely resist the slide, so it runs much farther and
   * stays on the world-frame line the robot entered on. Scale is set by the
   * lateralMomentum slider; the wheel type shapes how pronounced it is.
   */
  function beginTurnCarry(): () => { dx: number; dy: number } {
    let v = vel.v * latMom * (omni ? 1.5 : 1);
    vel.v = 0; // the drive channel commands no translation during a turn
    const hr0 = pose.h * D2R;
    const wx = Math.sin(hr0);
    const wy = Math.cos(hr0);
    const decay = latAccel * (omni ? 0.35 : 1);
    return () => {
      if (Math.abs(v) < 0.05) return { dx: 0, dy: 0 };
      const hr = pose.h * D2R;
      const ux = omni ? wx : Math.sin(hr);
      const uy = omni ? wy : Math.cos(hr);
      const dx = ux * v * DT;
      const dy = uy * v * DT;
      v -= Math.sign(v) * Math.min(Math.abs(v), decay * DT);
      return { dx, dy };
    };
  }

  // Per-channel governor: cap + min floor + slew, from the assignable PID cfg.
  const govLast = { drive: 0, turn: 0, heading: 0 };
  function govern(chan: Chan, raw: number): number {
    const cfg = pid[chan];
    let out = clamp(raw, Math.max(0.1, cfg.maxOutput));
    if (cfg.minOutput > 0 && Math.abs(out) > 1e-4 && Math.abs(out) < cfg.minOutput) {
      out = Math.sign(out) * cfg.minOutput;
    }
    const last = govLast[chan];
    const d = out - last;
    if (Math.abs(out) > Math.abs(last)) {
      if (cfg.maxAccelRate > 0 && Math.abs(d) > cfg.maxAccelRate) out = last + Math.sign(d) * cfg.maxAccelRate;
    } else {
      if (cfg.maxDecelRate > 0 && Math.abs(d) > cfg.maxDecelRate) out = last + Math.sign(d) * cfg.maxDecelRate;
    }
    govLast[chan] = out;
    return out;
  }

  // TotalOutput stage: slew each SIDE toward its last value, ratio-preserving
  // cap at maxOutput, then the min floor — mirrors governOutputs() in the lib.
  const totLast = { l: 0, r: 0 };
  function totSlew(raw: number, last: number): number {
    const d = raw - last;
    if (Math.abs(raw) > Math.abs(last)) {
      if (tout.maxAccelRate > 0 && Math.abs(d) > tout.maxAccelRate) return last + Math.sign(d) * tout.maxAccelRate;
    } else {
      if (tout.maxDecelRate > 0 && Math.abs(d) > tout.maxDecelRate) return last + Math.sign(d) * tout.maxDecelRate;
    }
    return raw;
  }
  /** apply the stage-2 governor to blended sides; returns effective drive/turn */
  function governTotal(dp: number, tp: number): { dp: number; tp: number } {
    let l = totSlew(dp + tp, totLast.l);
    let r = totSlew(dp - tp, totLast.r);
    const maxMag = Math.max(Math.abs(l), Math.abs(r));
    if (tout.maxOutput > 0 && maxMag > tout.maxOutput) {
      const s = tout.maxOutput / maxMag;
      l *= s;
      r *= s;
    }
    if (tout.minOutput > 0) {
      if (Math.abs(l) < tout.minOutput && l !== 0) l = Math.sign(l) * tout.minOutput;
      if (Math.abs(r) < tout.minOutput && r !== 0) r = Math.sign(r) * tout.minOutput;
    }
    totLast.l = l;
    totLast.r = r;
    return { dp: (l + r) / 2, tp: (l - r) / 2 };
  }

  /**
   * Synthesized P/I/D term breakdown for the replay-style graphs. P = kp·e is
   * the honest proportional term; I and D use plausible fixed ratios of kp
   * (ki = 0.04·kp, kd = 0.08·kp linear / 0.01·kp angular) — clean, noise-free,
   * reset per movement.
   */
  function makeSynth(kp: number, angular: boolean) {
    let iAcc = 0;
    let prev: number | null = null;
    const ki = kp * 0.04;
    const kd = kp * (angular ? 0.01 : 0.08);
    return (e: number) => {
      iAcc += e * DT;
      const p = clamp(kp * e, 12);
      const i = clamp(ki * iAcc, 3);
      const d = prev === null ? 0 : clamp(kd * ((e - prev) / DT), 6);
      prev = e;
      return { p, i, d };
    };
  }
  let dSynth = makeSynth(knobs.driveKp, false);
  let tSynth = makeSynth(knobs.headingKp, true);

  /**
   * Integrate one unicycle step at drive/turn volts. The turn output of a
   * lateral movement is heading correction — governed by headingPID (default),
   * NOT turnPID — then the blended sides pass the totalOutput stage. Velocity
   * approaches the commanded value at the derived accel limits.
   */
  function step(ticks: SimTick[], drivePow: number, turnPow: number, turnChan: Chan = 'heading', err?: { de: number; he: number }) {
    // fresh movement → fresh I accumulator + D history
    if (ticks.length === 0) {
      dSynth = makeSynth(knobs.driveKp, false);
      tSynth = makeSynth(knobs.headingKp, true);
    }
    const dpid = err ? dSynth(err.de) : undefined;
    const tpid = err ? tSynth(err.he) : undefined;
    const g = governTotal(govern('drive', drivePow), govern(turnChan, turnPow));
    const vCmd = (g.dp / 12) * maxSpeedEff;
    const wCmd = (g.tp / 12) * maxWEff;
    vel.v += clamp(vCmd - vel.v, latAccel * DT);
    vel.w += clamp(wCmd - vel.w, angAccel * DT);
    pose.h += vel.w * DT;
    const hr = pose.h * D2R;
    pose.x += vel.v * Math.sin(hr) * DT;
    pose.y += vel.v * Math.cos(hr) * DT;
    const dsv = Math.abs(vel.v * DT);
    const dthr = Math.abs(vel.w * DT * D2R);
    lastArcRadius = dthr > 1e-5 && dsv > 1e-6 ? dsv / dthr : 1e4;
    ticks.push({
      t,
      x: pose.x,
      y: pose.y,
      h: pose.h,
      de: err?.de,
      he: err?.he,
      dp: g.dp,
      tp: g.tp,
      dP: dpid?.p,
      dI: dpid?.i,
      dD: dpid?.d,
      tP: tpid?.p,
      tI: tpid?.i,
      tD: tpid?.d
    });
    t += TICK;
  }

  /**
   * Per-movement exit state. `lastV` is the previous tick's chassis speed, which is
   * what the new settle test compares against ("am I slower than I was?"). It starts
   * at 0 exactly like the lib's lastV/lastω globals, which clearAllValues() zeroes on
   * every movement entry — so settle can never fire on a movement's first tick and
   * always needs two real samples. That is what stops a hot handoff, where the robot
   * is already decelerating as the next movement starts, from settling immediately.
   */
  type ExitState = { start: number; lastV: number };
  const newExitState = (): ExitState => ({ start: t, lastV: 0 });

  /**
   * Lateral exit — mirrors checkLateralExit() in exitConditions.cpp. -1 is the OFF
   * sentinel on every field.
   *  - SETTLE (v = -1 → off): the chassis is SLOWING (|v| < |lastV|) AND the error is
   *    inside settleGate AND |v| is under ec.v. This is a speed test now, not the PID
   *    D-term test it used to be.
   *  - CROSS (d = -1 → off, or no point target): which SIDE of the target's ⊥ line
   *    we're on, not how close to it. The line is INFINITE (no lateral span to miss)
   *    and runs ⊥ to the robot's heading through the target; `along` starts positive
   *    with the line ahead and goes negative once past, so a fast pass can't skip it
   *    the way a proximity band can. d slides the line that many inches TOWARD us to
   *    fire early (0 = line sits on the target).
   * `hasPointTarget` = false for drive(), which is relative and has no target point —
   * the lib skips cross there rather than testing a line through the field origin.
   * `err` is the movement's SIGNED lateralError, incl. drivePID.targetOffset.
   */
  function latExit(
    ec: LatEc,
    st: ExitState,
    err: number,
    tx: number,
    ty: number,
    backward = false,
    hasPointTarget = true
  ): string | null {
    const vNow = vel.v;
    if (ec.t !== -1 && t - st.start >= ec.t) return 'timeout';
    if (t - st.start >= HARD_CAP_MS) return 'timeout';
    if (
      ec.v !== -1 &&
      Math.abs(vNow) < Math.abs(st.lastV) &&
      Math.abs(err) < ec.settleGate &&
      Math.abs(vNow) < ec.v
    )
      return 'settle';
    if (hasPointTarget && ec.d !== -1) {
      const hr = pose.h * D2R;
      let along = (tx - pose.x) * Math.sin(hr) + (ty - pose.y) * Math.cos(hr); // + = line ahead, − = passed it
      if (backward) along = -along; // measure along the way we travel
      // SIM-ONLY 0.1" allowance, NOT in the lib. A real robot always overshoots the
      // line it's driving at — noise and momentum flip the sign. This sim is
      // noise-free, so a P controller decelerating into the target makes `along`
      // asymptote to 0 from above and the side test never trips: d = 0 would
      // sit there until the timeout. d > 0 doesn't need it (the line is behind
      // the target, so the robot drives clean through).
      if (along <= ec.d + 0.1) return 'perpLine';
    }
    st.lastV = vNow; // only on a non-exiting tick, like the lib
    return null;
  }

  /**
   * Turn exit — mirrors checkTurnExit() in exitConditions.cpp. Same shape as the
   * lateral channel on the angular axis.
   *  - SETTLE (v = -1 → off): |ω| is falling AND the error is inside settleGate AND
   *    |ω| is under ec.v. NOTE ec.v is in RAD/s here, because the lib compares against
   *    chassis.ω, which updateVelocity() computes in radians — while settleGate is in
   *    degrees. vel.w is degrees/s in this sim, hence the D2R.
   *  - ANGLE (d = -1 → off): ungated — fires mid-rotation the moment |error| <= d,
   *    without waiting for the robot to slow.
   */
  function turnExit(ec: TurnEc, st: ExitState, turnError: number): string | null {
    const wNow = vel.w * D2R; // lib compares chassis.ω, which is rad/s
    if (ec.t !== -1 && t - st.start >= ec.t) return 'timeout';
    if (t - st.start >= HARD_CAP_MS) return 'timeout';
    if (
      ec.v !== -1 &&
      Math.abs(wNow) < Math.abs(st.lastV) &&
      Math.abs(turnError) < ec.settleGate &&
      Math.abs(wNow) < ec.v
    )
      return 'settle';
    if (ec.d !== -1 && Math.abs(turnError) <= ec.d) return 'angle';
    st.lastV = wNow;
    return null;
  }

  // Configurable EC defaults (left panel) applied to omitted initializer fields.
  const latDefaults = { t: knobs.ecLatT, v: knobs.ecLatV, d: knobs.ecLatD, settleGate: knobs.ecLatGate };
  const turnDefaults = { t: knobs.ecTurnT, v: knobs.ecTurnV, d: knobs.ecTurnD, settleGate: knobs.ecTurnGate };
  const latEc = (a: ArgValue | undefined) => structVals('lateralEc', a, latDefaults) as unknown as LatEc;
  const turnEc = (a: ArgValue | undefined) => structVals('turnEc', a, turnDefaults) as unknown as TurnEc;

  // --- movement primitives (each returns exit reason) ---

  function runFace(targetH: number, ec: TurnEc, direction: number, ticks: SimTick[]): string {
    let sweep = wrap(targetH - pose.h);
    // turnPID.targetOffset: bias the target along the turn direction
    const off = takeOffset('turn');
    if (off !== 0) sweep += Math.sign(sweep || 1) * off;
    if (direction === 1 && sweep < 0) sweep += 360;
    if (direction === -1 && sweep > 0) sweep -= 360;
    // A properly-tuned turn PID does NOT overshoot its target. Rather than a
    // naive P (which blows through the target when the top speed is high relative
    // to the accel), the controller commands the fastest speed it can still brake
    // FROM to stop exactly at the target: wCmd = min(vMax, sqrt(2·a·error)). This
    // cruises while far and decelerates just in time — no overshoot, whatever the
    // speed/accel knobs are. Acceleration is still plant-limited (not eased).
    const targetAbs = pose.h + sweep;
    // top turn speed honors BOTH the turnPID cap and the totalOutput cap
    const voltCap = Math.min(Math.max(0.6, pid.turn.maxOutput), tout.maxOutput > 0 ? tout.maxOutput : 12);
    const vMax = maxWEff * (voltCap / 12);
    const est = newExitState();
    // motion chaining: an early-exited lateral's leftover speed slides through
    const carryStep = beginTurnCarry();
    const turnSynth = makeSynth(knobs.turnKp, true); // P/I/D breakdown for the graphs
    for (;;) {
      const remaining = targetAbs - pose.h;
      const absRem = Math.abs(remaining);
      const exit = turnExit(ec, est, remaining);
      if (exit) return exit;
      // anticipate one tick of travel so speed stays UNDER the braking curve
      const brakeW = Math.sqrt(2 * angAccel * Math.max(0, absRem - Math.abs(vel.w) * DT));
      // turnKp sets the P-approach aggression; the brake curve still guarantees
      // a no-overshoot stop whatever the gain
      const pW = (clamp(absRem * knobs.turnKp, 12) / 12) * vMax;
      const wCmd = (Math.sign(remaining) || 1) * Math.min(pW, brakeW);
      vel.w += clamp(wCmd - vel.w, angAccel * DT);
      const nextH = pose.h + vel.w * DT;
      // a controlled stop lands exactly on target — clamp the sub-tick crossing;
      // but let a genuine excess-speed entry (carried momentum) overshoot through
      if ((targetAbs - pose.h) * (targetAbs - nextH) < 0 && Math.abs(vel.w) <= brakeW + angAccel * DT * 1.5) {
        pose.h = targetAbs;
        vel.w = 0;
      } else {
        pose.h = nextH;
      }
      const cd = carryStep();
      pose.x += cd.dx;
      pose.y += cd.dy;
      const ts = turnSynth(targetAbs - pose.h);
      ticks.push({ t, x: pose.x, y: pose.y, h: pose.h, de: 0, he: targetAbs - pose.h, dp: 0, tp: (vel.w / maxWEff) * 12, tP: ts.p, tI: ts.i, tD: ts.d });
      t += TICK;
    }
  }

  function runSwing(side: 'left' | 'right', targetH: number, ec: TurnEc, partiality: number, direction: number, ticks: SimTick[]): string {
    let sweep = wrap(targetH - pose.h);
    const off = takeOffset('turn');
    if (off !== 0) sweep += Math.sign(sweep || 1) * off;
    if (direction === 1 && sweep < 0) sweep += 360;
    if (direction === -1 && sweep > 0) sweep -= 360;
    // Same closed-loop turn PID + early-exit rule as runFace, but the body pivots
    // about the locked side rather than spinning in place.
    const targetAbs = pose.h + sweep;
    const voltCap = Math.min(Math.max(0.6, pid.turn.maxOutput), tout.maxOutput > 0 ? tout.maxOutput : 12);
    // Partiality matches LucasLib: free side drives +v, locked side +p·v (same
    // sign), so the differential ω ∝ (1−p) and the ICR sits at distance
    // d = (w/2)·(1+p)/(1−p) from center on the locked side.
    //   p = −1 → ICR at center → full SPOT turn (ω = spot)
    //   p =  0 → ICR at the locked wheel → SWING (ω = spot/2)
    //   p → +1 → ICR to infinity → nearly STRAIGHT (ω → 0)
    const p = Math.max(-0.98, Math.min(0.95, partiality));
    const w2 = knobs.trackwidth / 2;
    const d = (w2 * (1 + p)) / (1 - p);
    // ω = spot·(1−p)/2 (spot turn drives both sides ±v → ω=2v/tw; here the
    // differential is v(1−p) → ω=v(1−p)/tw = spot·(1−p)/2).
    const vMax = maxWEff * (voltCap / 12) * ((1 - p) / 2);
    const hr0 = pose.h * D2R;
    const lockX = side === 'left' ? -Math.cos(hr0) : Math.cos(hr0);
    const lockY = side === 'left' ? Math.sin(hr0) : -Math.sin(hr0);
    let pivotX = pose.x + lockX * d;
    let pivotY = pose.y + lockY * d;
    const est = newExitState();
    // motion chaining: leftover drive speed slides the whole pivot geometry
    const carryStep = beginTurnCarry();
    const turnSynth = makeSynth(knobs.swingKp, true); // P/I/D breakdown for the graphs
    for (;;) {
      const remaining = targetAbs - pose.h;
      const absRem = Math.abs(remaining);
      const exit = turnExit(ec, est, remaining);
      if (exit) return exit;
      // anticipate one tick of travel so speed stays UNDER the braking curve
      const brakeW = Math.sqrt(2 * angAccel * Math.max(0, absRem - Math.abs(vel.w) * DT));
      // swingKp sets the P-approach aggression (separate from turns so it can be
      // cranked); the brake curve still guarantees a no-overshoot stop whatever the gain
      const pW = (clamp(absRem * knobs.swingKp, 12) / 12) * vMax;
      const wCmd = (Math.sign(remaining) || 1) * Math.min(pW, brakeW);
      vel.w += clamp(wCmd - vel.w, angAccel * DT);
      const nextH = pose.h + vel.w * DT;
      if ((targetAbs - pose.h) * (targetAbs - nextH) < 0 && Math.abs(vel.w) <= brakeW + angAccel * DT * 1.5) {
        pose.h = targetAbs;
        vel.w = 0;
      } else {
        pose.h = nextH;
      }
      const cd = carryStep();
      pivotX += cd.dx;
      pivotY += cd.dy;
      const hr = pose.h * D2R;
      const offX = side === 'left' ? Math.cos(hr) : -Math.cos(hr);
      const offY = side === 'left' ? -Math.sin(hr) : Math.sin(hr);
      pose.x = pivotX + offX * d;
      pose.y = pivotY + offY * d;
      const ts = turnSynth(targetAbs - pose.h);
      ticks.push({ t, x: pose.x, y: pose.y, h: pose.h, de: 0, he: targetAbs - pose.h, dp: 0, tp: (vel.w / maxWEff) * 12, tP: ts.p, tI: ts.i, tD: ts.d });
      t += TICK;
    }
  }

  function runDrive(distance: number, ec: LatEc, heading: number, cosineScaling: boolean, effort: number, ticks: SimTick[]): string {
    vel.w *= angMom; // motion chaining: leftover spin swings through the first split second
    // drive() is signed: negative distance backs up, and it never reads the global
    // backward. targetOffset is applied ALONG the direction of travel, which makes
    // its sign direction-independent — positive overshoots, negative stops short,
    // whether distance is +20 or -20.
    const off = takeOffset('drive');
    const target = distance + off * Math.sign(distance);
    const hux = Math.sin(heading * D2R);
    const huy = Math.cos(heading * D2R);
    const x0 = pose.x;
    const y0 = pose.y;
    const est = newExitState();
    for (;;) {
      const traveled = (pose.x - x0) * hux + (pose.y - y0) * huy;
      const le = target - traveled;
      // hasPointTarget = false — a relative drive has no target point, so CROSS is
      // skipped. The lib then ORs in a plain remaining-distance tolerance on ec.d
      // (fabs(lateralError) < ec.d, which is simply false while d is -1).
      const exit = latExit(ec, est, le, 0, 0, false, false) ?? (Math.abs(le) < ec.d ? 'dist' : null);
      if (exit) return exit;
      const he = wrap(heading - pose.h);
      const dp = clamp(le * knobs.driveKp, 12) * (cosineScaling ? Math.max(0.2, Math.cos(he * D2R)) : 1);
      const tp = clamp(he * knobs.headingKp * effort, 12);
      step(ticks, dp, tp, 'heading', { de: le, he });
    }
  }

  function runMoveTo(tx: number, ty: number, ec: LatEc, cosineScaling: boolean, effort: number, ticks: SimTick[]): string {
    vel.w *= angMom; // motion chaining: leftover spin swings through the first split second
    const backward = gBack;
    const off = takeOffset('drive');
    const est = newExitState();
    let close = false;
    for (;;) {
      const dx = tx - pose.x;
      const dy = ty - pose.y;
      // driveError = distanceTo(target) + targetOffset (dist already carries off)
      const dist = Math.hypot(dx, dy) + off;
      const exit = latExit(ec, est, dist, tx, ty, backward);
      if (exit) return exit;
      let pointHeading = (Math.atan2(dx, dy) * 180) / Math.PI;
      if (backward) pointHeading += 180;
      let he = wrap(pointHeading - pose.h);
      if (dist < knobs.headingDisableDist) close = true;
      if (close) he = 0;
      const mag = clamp(dist * knobs.driveKp, 12) * (cosineScaling ? Math.max(0, Math.cos(he * D2R)) : 1);
      const dp = backward ? -mag : mag;
      const tp = clamp(he * knobs.headingKp * effort, 12);
      step(ticks, dp, tp, 'heading', { de: dist, he });
    }
  }

  /** lead point of a BoomParams under the CURRENT global backward (getBoomLeadX/Y). */
  function boomLead(bv: Record<string, number | boolean>): { x: number; y: number } {
    const bx = bv.x as number;
    const by = bv.y as number;
    const lead = bv.lead as number;
    const leadSign = gBack ? 1 : -1;
    const d = lead * Math.hypot(bx - pose.x, by - pose.y);
    return {
      x: bx + leadSign * Math.sin((bv.heading as number) * D2R) * d,
      y: by + leadSign * Math.cos((bv.heading as number) * D2R) * d
    };
  }

  function runBoomerang(
    bv: Record<string, number | boolean>,
    ec: LatEc,
    ticks: SimTick[]
  ): { exit: string; boom: BoomOverlay } {
    vel.w *= angMom; // motion chaining: leftover spin swings through
    const backward = gBack;
    // NOTE: the lib boomerang never reads (or zeroes) drivePID.targetOffset —
    // it's not in the boomerang loop — so the sandbox doesn't consume it here.
    const targetX = bv.x as number;
    const targetY = bv.y as number;
    const targetH = bv.heading as number;
    const lead = bv.lead as number;
    const gLead = bv.gLead as number;
    const chaseLimit = bv.leadChasePowerLimit as number;

    const P0 = { x: pose.x, y: pose.y };
    const initialDist = Math.hypot(targetX - P0.x, targetY - P0.y);
    const initialLeadDist = lead * initialDist;
    const leadSign = backward ? 1 : -1;
    const dirX = leadSign * Math.sin(targetH * D2R);
    const dirY = leadSign * Math.cos(targetH * D2R);
    const initCarrotX = targetX + dirX * initialLeadDist;
    const initCarrotY = targetY + dirY * initialLeadDist;

    let carrotX = initCarrotX;
    let carrotY = initCarrotY;
    const initCarrotDist = Math.hypot(initCarrotX - P0.x, initCarrotY - P0.y);
    let minCarrotDist = initCarrotDist;
    // speed-scaled ghost->carrot switch distance (lib: Daniel's 2·maxSpeed with
    // maxSpeed recovered as a fraction of the 12 V full scale)
    const ghostSwitchDist = 2.0 * (tout.maxOutput / 12);
    let closeGhost = false;
    let closeEnd = false;
    const carrotTrack: { x: number; y: number }[] = [];
    const ghostTrack: { x: number; y: number }[] = [];
    const est = newExitState();
    let exit = 'settle';

    for (;;) {
      const curCarrotDist = Math.hypot(carrotX - pose.x, carrotY - pose.y);
      if (curCarrotDist < minCarrotDist) minCarrotDist = curCarrotDist;
      const s = initCarrotDist > 0 ? minCarrotDist / initCarrotDist : 0;
      carrotX = targetX + dirX * s * initialLeadDist;
      carrotY = targetY + dirY * s * initialLeadDist;
      const ghostX = initCarrotX + (carrotX - initCarrotX) * (1 - gLead);
      const ghostY = initCarrotY + (carrotY - initCarrotY) * (1 - gLead);
      carrotTrack.push({ x: carrotX, y: carrotY });
      ghostTrack.push({ x: ghostX, y: ghostY });

      const distEnd = Math.hypot(targetX - pose.x, targetY - pose.y);
      const distCarrot = Math.hypot(carrotX - pose.x, carrotY - pose.y);
      const distGhost = Math.hypot(ghostX - pose.x, ghostY - pose.y);
      // exit on the true distance to the target (no targetOffset — lib boomerang
      // passes lateralError straight through)
      const ecExit = latExit(ec, est, distEnd, targetX, targetY, backward);
      if (ecExit) {
        exit = ecExit;
        break;
      }

      let aimX = ghostX;
      let aimY = ghostY;
      if (distCarrot < 0.7 || closeEnd) {
        closeEnd = true;
        aimX = targetX;
        aimY = targetY;
      } else if (distGhost < ghostSwitchDist || closeGhost) {
        closeGhost = true;
        aimX = carrotX;
        aimY = carrotY;
      }
      let aimHeading = (Math.atan2(aimX - pose.x, aimY - pose.y) * 180) / Math.PI;
      if (backward) aimHeading += 180;
      let he = wrap(aimHeading - pose.h);
      if (closeEnd && distEnd < 0.7) he = wrap(targetH - pose.h);

      let maxSlip = Infinity;
      if (chaseLimit > 0) maxSlip = Math.sqrt(chaseLimit * Math.abs(lastArcRadius));

      // SPEED tracks the true target distance (lib: the carrot only when it is
      // the farther waypoint and the end isn't latched)
      const speedDist = closeEnd || distEnd > distCarrot ? distEnd : distCarrot;
      let mag = clamp(speedDist * knobs.driveKp, 12) * Math.max(0, Math.cos(he * D2R));
      if (closeEnd && distEnd < 0.7) {
        // settle overshoot brake (Daniel's settle cosine): steering holds the
        // absolute heading here, so re-introduce the bearing-to-target cosine —
        // it flips drive once the target falls behind, pulling the robot back
        let pointHe = (Math.atan2(targetX - pose.x, targetY - pose.y) * 180) / Math.PI;
        if (backward) pointHe += 180;
        mag = clamp(speedDist * knobs.driveKp, 12) * Math.cos(wrap(pointHe - pose.h) * D2R);
      }
      if (chaseLimit > 0 && !(closeEnd && distEnd < 0.7) && Math.abs(mag) > maxSlip) mag = Math.sign(mag) * maxSlip;
      const dp = backward ? -mag : mag;
      const tp = clamp(he * 0.8 * knobs.headingKp, 12);
      step(ticks, dp, tp, 'heading', { de: distEnd, he });
      // live boomerang geometry for the replay-identical overlay
      const stage = closeEnd ? 2 : closeGhost ? 1 : 0;
      Object.assign(ticks[ticks.length - 1], { carrotX, carrotY, ghostX, ghostY, stage, arcR: lastArcRadius });
    }

    return {
      exit,
      boom: {
        target: { x: targetX, y: targetY },
        heading: targetH,
        lead,
        gLead,
        backward,
        initCarrot: { x: initCarrotX, y: initCarrotY },
        carrotTrack,
        ghostTrack,
        perpLine: ec.d
      }
    };
  }

  // --- walk the statements ---

  for (let i = 0; i < parsed.stmts.length; i++) {
    const s = parsed.stmts[i];

    // assignments take effect for everything below them
    if (s.kind === 'assign') {
      if (s.target === 'backward') {
        gBack = s.value.value === true;
      } else if (s.target === 'totalOutput' && typeof s.value.value === 'number') {
        const cfg = tout as unknown as Record<string, number>;
        if (s.prop in cfg) cfg[s.prop] = s.value.value;
      } else if (
        (s.target === 'drivePID' || s.target === 'turnPID' || s.target === 'headingPID') &&
        typeof s.value.value === 'number'
      ) {
        const chan: Chan = s.target === 'drivePID' ? 'drive' : s.target === 'turnPID' ? 'turn' : 'heading';
        const cfg = pid[chan] as unknown as Record<string, number>;
        if (s.prop in cfg) cfg[s.prop] = s.value.value;
      } else if (s.target === 'totalOutput' && typeof s.value.value === 'number') {
        // stage-2 governor on the blended left/right sides
        const cfg = tout as unknown as Record<string, number>;
        if (s.prop in cfg) cfg[s.prop] = s.value.value;
      }
      continue;
    }

    // [TRAJECTORY] marker — bake the stored spline (anchor 0 pinned to the live
    // pose) and drive the robot along the planned path. v1 fidelity: the robot
    // follows the plan (RAMSETE with ~zero initial error ≈ the planned path).
    if (s.kind === 'trajectory') {
      if (s.name !== 'TRAJECTORY') continue;
      const td = trajectories[i];
      const anchors = (td ? td.anchors : defaultTrajectory(pose)).map((a) => ({ ...a }));
      anchors[0] = { ...anchors[0], x: pose.x, y: pose.y }; // pin start to the robot
      const prm = td?.params ?? { maxVel: 60, exitVel: 0, maxAccel: 60, beta: 2, zeta: 0.7 };
      const baked = bakeTrajectory(
        anchors,
        { maxVel: prm.maxVel, maxAccel: prm.maxAccel, startVel: Math.abs(vel.v), exitVel: prm.exitVel },
        knobs.trackwidth
      );
      if (baked.points.length) {
        const ticks: SimTick[] = [];
        const start = { ...pose };
        const pidsSnap: PidSnapshot = {
          drive: { ...pid.drive },
          turn: { ...pid.turn },
          heading: { ...pid.heading },
          total: { ...tout }
        };
        for (let k = 0; k < baked.points.length; k++) {
          const bp = baked.points[k];
          if (k > 0) {
            const ds = bp.s - baked.points[k - 1].s;
            const vAvg = Math.max(1, (bp.v + baked.points[k - 1].v) / 2);
            t += (ds / vAvg) * 1000; // ms to cover this arc step at the planned speed
          }
          pose.x = bp.x;
          pose.y = bp.y;
          pose.h = bp.h;
          vel.v = bp.v;
          ticks.push({ t, x: pose.x, y: pose.y, h: pose.h, de: 0, he: 0, dp: (bp.v / maxSpeedEff) * 12, tp: 0 });
        }
        vel.w = 0;
        segments.push({
          stmtIndex: i,
          fn: 'followTrajectory',
          ticks,
          start,
          end: { ...pose },
          exit: 'settle',
          backward: false,
          pids: pidsSnap
        });
      }
      continue;
    }

    if (s.kind !== 'call') continue;

    if (s.fn === 'chassis.setPos') {
      if (s !== first) {
        pose.x = argNum(s.args[0], pose.x);
        pose.y = argNum(s.args[1], pose.y);
        pose.h = argNum(s.args[2], pose.h);
      }
      continue;
    }

    const ticks: SimTick[] = [];
    const start = { ...pose };
    const ranBack = gBack;
    // governor configs in effect as this movement STARTS (targetOffset not yet consumed)
    const pidsSnap: PidSnapshot = { drive: { ...pid.drive }, turn: { ...pid.turn }, heading: { ...pid.heading }, total: { ...tout } };
    let exit = 'settle';
    let boom: BoomOverlay | undefined;

    switch (s.fn) {
      case 'face': {
        const ec = turnEc(s.args[1]);
        exit = runFace(argNum(s.args[0], pose.h), ec, argNum(s.args[2], 0), ticks);
        break;
      }
      case 'facePoint': {
        const a0 = s.args[0];
        if (a0?.kind === 'ident' && a0.name !== 'chassis.h') {
          // overload: face a BoomParams target's LEAD point (honors global backward)
          const decl = parsed.booms[a0.name];
          const bv = decl ? boomVals(decl.init) : null;
          if (bv) {
            const lp = boomLead(bv);
            const ec = turnEc(s.args[1]);
            let hTarget = (Math.atan2(lp.x - pose.x, lp.y - pose.y) * 180) / Math.PI;
            if (gBack) hTarget += 180;
            exit = runFace(hTarget, ec, argNum(s.args[2], 0), ticks);
          }
        } else {
          const ec = turnEc(s.args[2]);
          let hTarget = (Math.atan2(argNum(s.args[0], 0) - pose.x, argNum(s.args[1], 0) - pose.y) * 180) / Math.PI;
          if (gBack) hTarget += 180;
          exit = runFace(hTarget, ec, argNum(s.args[3], 0), ticks);
        }
        break;
      }
      case 'swingOnLeft':
      case 'swingOnRight': {
        const ec = turnEc(s.args[1]);
        exit = runSwing(
          s.fn === 'swingOnLeft' ? 'left' : 'right',
          argNum(s.args[0], pose.h),
          ec,
          argNum(s.args[2], 0),
          argNum(s.args[3], 0),
          ticks
        );
        break;
      }
      case 'swingOnLeftToPoint':
      case 'swingOnRightToPoint': {
        const ec = turnEc(s.args[2]);
        let hTarget = (Math.atan2(argNum(s.args[0], 0) - pose.x, argNum(s.args[1], 0) - pose.y) * 180) / Math.PI;
        if (gBack) hTarget += 180;
        exit = runSwing(
          s.fn === 'swingOnLeftToPoint' ? 'left' : 'right',
          hTarget,
          ec,
          argNum(s.args[3], 0),
          argNum(s.args[4], 0),
          ticks
        );
        break;
      }
      case 'drive': {
        const heading = s.args[1] && s.args[1].kind === 'num' ? s.args[1].lit.value : pose.h;
        const ec = latEc(s.args[2]);
        const bs = structVals('blend', s.args[3]);
        exit = runDrive(
          argNum(s.args[0], 0),
          ec,
          heading,
          bs.cosineScaling as boolean,
          efforts[i] ?? knobs.driveHeadingEffort,
          ticks
        );
        break;
      }
      case 'moveTo': {
        const ec = latEc(s.args[2]);
        const bs = structVals('blend', s.args[3]);
        exit = runMoveTo(
          argNum(s.args[0], 0),
          argNum(s.args[1], 0),
          ec,
          bs.cosineScaling as boolean,
          efforts[i] ?? knobs.moveToHeadingEffort,
          ticks
        );
        break;
      }
      case 'boomerangTo': {
        const a0 = s.args[0];
        let bv: Record<string, number | boolean> | null = null;
        if (a0?.kind === 'ident' && parsed.booms[a0.name]) bv = boomVals(parsed.booms[a0.name].init);
        else if (a0?.kind === 'struct') bv = boomVals(a0.init);
        if (bv) {
          const ec = latEc(s.args[1]);
          const r = runBoomerang(bv, ec, ticks);
          exit = r.exit;
          boom = r.boom;
        }
        break;
      }
      case 'voldrive': {
        const left = argNum(s.args[0], 0);
        const right = s.args[1] ? argNum(s.args[1], left) : left;
        pendingVol = { left, right };
        exit = 'timed';
        break;
      }
      case 'delay':
      case 'wait': {
        const unitsArg = s.args[1];
        const secUnits = s.fn === 'wait' && unitsArg?.kind === 'ident' && (unitsArg.name === 'sec' || unitsArg.name === 'seconds');
        const ms = argNum(s.args[0], 0) * (secUnits ? 1000 : 1);
        const steps = Math.max(0, Math.round(ms / TICK));
        const vol = pendingVol;
        // A bare delay/wait means the motors are already stopped — every movement
        // ends on voltageDrive(0,0) — so the robot coasts to rest and NOTHING carries
        // into the next movement. Kill it outright rather than modelling the decay:
        // even one tick of a stopped drivetrain sheds most of it.
        // Two exceptions: a delay with a voldrive still commanded is actively driving
        // (its speed must carry), and delay(0) advances no time at all.
        if (!vol && steps > 0) {
          vel.v = 0;
          vel.w = 0;
        }
        for (let k = 0; k < steps; k++) {
          let dpv = 0;
          let tpv = 0;
          if (vol) {
            dpv = clamp((vol.left + vol.right) / 2, 12);
            tpv = clamp((vol.left - vol.right) / 2, 12);
            const vCmd = (dpv / 12) * maxSpeedEff;
            const wCmd = (tpv / 12) * maxWEff;
            vel.v += clamp(vCmd - vel.v, latAccel * DT);
            vel.w += clamp(wCmd - vel.w, angAccel * DT);
            pose.h += vel.w * DT;
            const hr = pose.h * D2R;
            pose.x += vel.v * Math.sin(hr) * DT;
            pose.y += vel.v * Math.cos(hr) * DT;
          }
          ticks.push({ t, x: pose.x, y: pose.y, h: pose.h, de: 0, he: 0, dp: dpv, tp: tpv });
          t += TICK;
        }
        exit = 'timed';
        break;
      }
      default:
        continue;
    }
    // a voldrive statement itself emits no ticks; volts persist into wait()/delay()
    if (s.fn !== 'voldrive' && s.fn !== 'wait' && s.fn !== 'delay') pendingVol = null;

    segments.push({
      stmtIndex: i,
      fn: s.fn,
      ticks,
      start,
      end: { ...pose },
      exit,
      backward: ranBack,
      boom,
      pids: pidsSnap
    });
  }

  return { segments, duration: t, startPose, endPose: { ...pose } };
}
