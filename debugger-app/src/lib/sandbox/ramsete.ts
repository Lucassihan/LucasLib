// A line-for-line TypeScript port of LucasLib's `ramsete/` module, so the
// sandbox follows a spline the same way the brain does.
//
//   include/ramsete/bezierTools.h   → the spline layer (exact heading + curvature)
//   src/ramsete/profile.cpp         → bakeTrajectory: the 2D motion profile
//   src/ramsete/trajectory.cpp      → the baked schedule + sampleTrajectory
//   src/ramsete/ramseteController.cpp → the tracking law and the adaptive clock
//   src/ramsete/velocityController.cpp → one channel of the velocity layer
//
// The library reads its constants off the global `ramsete` object and the robot
// pose off the global `chassis`; here both are arguments, because the sandbox
// runs many simulated robots per keystroke. NOTHING else is changed — when this
// file and the C++ disagree, the C++ is right (see LucasLib is the source of
// truth). Keep the two in step: the deviations list in the library's own docs
// applies to this port as well, so do not "fix" anything that looks unlike the
// design study.

const D2R = Math.PI / 180;
const R2D = 180 / Math.PI;

export interface Vec2 {
  x: number;
  y: number;
}

/** LucasLib's Pose — heading is optional on a control point (util.h). */
export interface Pose {
  x: number;
  y: number;
  h?: number;
}

/** util.cpp sinc: the series near zero, sin(x)/x elsewhere. */
export function sinc(x: number): number {
  if (Math.abs(x) < 1e-3) return 1 - (x * x) / 6;
  return Math.sin(x) / x;
}

/** Folds an angle into (-180, 180]. */
export function wrapDegrees(angle: number): number {
  let a = angle;
  while (a > 180) a -= 360;
  while (a <= -180) a += 360;
  return a;
}

// =========================================================================
// bezierTools — position, tangent and curvature from the SAME four points.
// Frame: field inches, headings in degrees, 0 = +Y, clockwise positive.
// =========================================================================

/** One cubic segment: endpoints a and d, handles b and c. -1 = profile default. */
export interface BezierSegment {
  a: Vec2;
  b: Vec2;
  c: Vec2;
  d: Vec2;
  reversed?: boolean;
  stopEnd?: boolean;
  maxVel?: number;
  maxAccel?: number;
}

export function bezierAt(s: BezierSegment, t: number): Vec2 {
  const u = 1 - t;
  const w0 = u * u * u;
  const w1 = 3 * u * u * t;
  const w2 = 3 * u * t * t;
  const w3 = t * t * t;
  return {
    x: w0 * s.a.x + w1 * s.b.x + w2 * s.c.x + w3 * s.d.x,
    y: w0 * s.a.y + w1 * s.b.y + w2 * s.c.y + w3 * s.d.y
  };
}

export function bezierD(s: BezierSegment, t: number): Vec2 {
  const u = 1 - t;
  return {
    x: 3 * u * u * (s.b.x - s.a.x) + 6 * u * t * (s.c.x - s.b.x) + 3 * t * t * (s.d.x - s.c.x),
    y: 3 * u * u * (s.b.y - s.a.y) + 6 * u * t * (s.c.y - s.b.y) + 3 * t * t * (s.d.y - s.c.y)
  };
}

export function bezierDD(s: BezierSegment, t: number): Vec2 {
  const u = 1 - t;
  return {
    x: 6 * u * (s.c.x - 2 * s.b.x + s.a.x) + 6 * t * (s.d.x - 2 * s.c.x + s.b.x),
    y: 6 * u * (s.c.y - 2 * s.b.y + s.a.y) + 6 * t * (s.d.y - 2 * s.c.y + s.b.y)
  };
}

/** atan2(p'x, p'y) IS the LucasLib convention — an angle from +Y toward +X. */
export function bezierHeading(s: BezierSegment, t: number): number {
  const d = bezierD(s, t);
  return Math.atan2(d.x, d.y) * R2D;
}

/**
 * Signed curvature in radians per inch, clockwise positive. The cross product is
 * (p'y·p''x − p'x·p''y): the mirror of the textbook math-frame order, because
 * LucasLib's heading runs the other way around the circle.
 */
export function bezierCurvature(s: BezierSegment, t: number): number {
  const d = bezierD(s, t);
  const dd = bezierDD(s, t);
  const speed = Math.hypot(d.x, d.y);
  if (speed < 1e-9) return 0;
  return (d.y * dd.x - d.x * dd.y) / (speed * speed * speed);
}

export function bezierLength(s: BezierSegment, samples = 50): number {
  let total = 0;
  let last = bezierAt(s, 0);
  for (let i = 1; i <= samples; i++) {
    const now = bezierAt(s, i / samples);
    total += Math.hypot(now.x - last.x, now.y - last.y);
    last = now;
  }
  return total;
}

// =========================================================================
// trajectory — the baked schedule, and how to read it.
// =========================================================================

/** One baked row. Rows sit ~2 inches of arc apart. */
export interface TrajPoint {
  t: number; // seconds since the trajectory started
  x: number;
  y: number;
  heading: number; // deg, the tangent, flipped 180° on reversed rows
  v: number; // in/s, signed — negative means this row is driven backward
  omega: number; // deg/s, = |v| · curvature
}

/** The reference state at one instant: a row interpolated to the current clock. */
export interface TrajState {
  x: number;
  y: number;
  heading: number;
  v: number;
  omega: number;
}

export function trajectoryDuration(traj: TrajPoint[]): number {
  return traj.length ? traj[traj.length - 1].t : 0;
}

/**
 * Linear interpolation between the two rows bracketing t, clamped to the ends.
 * Heading interpolates along the WRAPPED difference so a row pair straddling
 * ±180° doesn't spin the reference the long way around.
 */
export function sampleTrajectory(traj: TrajPoint[], t: number): TrajState {
  const empty: TrajState = { x: 0, y: 0, heading: 0, v: 0, omega: 0 };
  if (!traj.length) return empty;
  const asState = (p: TrajPoint): TrajState => ({ x: p.x, y: p.y, heading: p.heading, v: p.v, omega: p.omega });
  if (t <= traj[0].t) return asState(traj[0]);
  if (t >= traj[traj.length - 1].t) return asState(traj[traj.length - 1]);

  let lo = 0;
  let hi = traj.length - 1;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (traj[mid].t <= t) lo = mid;
    else hi = mid;
  }
  const a = traj[lo];
  const b = traj[hi];
  const span = b.t - a.t;
  const u = span > 1e-9 ? (t - a.t) / span : 0;
  return {
    x: a.x + (b.x - a.x) * u,
    y: a.y + (b.y - a.y) * u,
    heading: a.heading + wrapDegrees(b.heading - a.heading) * u,
    v: a.v + (b.v - a.v) * u,
    omega: a.omega + (b.omega - a.omega) * u
  };
}

// =========================================================================
// profile — three velocity ceilings, two passes, one integration.
// =========================================================================

/** ProfileConfig (profile.h). -1 means "fall back to the chassis limit". */
export interface ProfileConfig {
  maxVel: number;
  maxAccel: number;
  maxDecel: number;
  trackwidth: number;
  velMargin: number;
  startSpeed: number;
  endSpeed: number;
  spacing: number;
  torqueSlope: number;
  torqueFloor: number;
}

export const DEFAULT_PROFILE: ProfileConfig = {
  maxVel: -1,
  maxAccel: -1,
  maxDecel: -1,
  trackwidth: -1,
  velMargin: 0.9,
  startSpeed: 0,
  endSpeed: 0,
  spacing: 2,
  torqueSlope: 0.8,
  torqueFloor: 0.15
};

/** The `Ramsete` struct's constants (config.cpp fills these in setRamseteSettings). */
export interface RamseteConfig {
  trackwidth: number;
  maxVel: number;
  maxAccel: number;
  b: number;
  zeta: number;
  vMinGain: number;
  clockCoupling: number;
}

export const DEFAULT_RAMSETE: RamseteConfig = {
  trackwidth: 13.5,
  maxVel: 55,
  maxAccel: 90,
  b: 0.03,
  zeta: 0.4,
  vMinGain: 4,
  clockCoupling: 6
};

// Below this speed the cruise branch of the time integration would divide by
// something near zero, so it borrows this floor instead.
const MIN_INTEGRATION_SPEED = 1;

interface ProfileSample {
  x: number;
  y: number;
  heading: number;
  curvature: number;
  ceiling: number;
  accel: number;
  step: number;
  reversed: boolean;
}

/**
 * The spline parameters that split a segment into `count` pieces of EQUAL ARC.
 * Stepping t uniformly does not do this — a cubic sweeps its parameter at wildly
 * different speeds depending on its handles.
 */
function arcSpacedParams(seg: BezierSegment, count: number): number[] {
  const fine = count * 8;
  const arc = new Array<number>(fine + 1).fill(0);

  let last = bezierAt(seg, 0);
  for (let i = 1; i <= fine; i++) {
    const now = bezierAt(seg, i / fine);
    arc[i] = arc[i - 1] + Math.hypot(now.x - last.x, now.y - last.y);
    last = now;
  }

  const params = new Array<number>(count + 1).fill(0);
  const total = arc[fine];
  let walk = 0;
  for (let j = 1; j <= count; j++) {
    const want = (total * j) / count;
    while (walk < fine && arc[walk + 1] < want) walk++;
    const span = arc[walk + 1] - arc[walk];
    const frac = span > 1e-9 ? (want - arc[walk]) / span : 0;
    params[j] = (walk + frac) / fine;
  }
  params[count] = 1;
  return params;
}

/** Fraction of the motor's torque still available at this fraction of free speed. */
function torqueFraction(speedFraction: number, cfg: ProfileConfig): number {
  return Math.max(1 - cfg.torqueSlope * Math.abs(speedFraction), cfg.torqueFloor);
}

/** -1 fields fall back to the chassis's own limits. */
export function resolveProfile(cfg: Partial<ProfileConfig>, rc: RamseteConfig): ProfileConfig {
  const out: ProfileConfig = { ...DEFAULT_PROFILE, ...cfg };
  if (out.maxVel < 0) out.maxVel = rc.maxVel;
  if (out.maxAccel < 0) out.maxAccel = rc.maxAccel;
  if (out.maxDecel < 0) out.maxDecel = out.maxAccel;
  if (out.trackwidth < 0) out.trackwidth = rc.trackwidth;
  return out;
}

function sampleCount(length: number, spacing: number): number {
  return Math.max(Math.ceil(length / Math.max(spacing, 0.25)), 5);
}

/** Ceilings 1 and 2, evaluated at one sample. */
function speedCeiling(curvature: number, seg: BezierSegment, cfg: ProfileConfig): number {
  let top = cfg.maxVel * cfg.velMargin;

  if ((seg.maxVel ?? -1) > 0) top = Math.min(top, seg.maxVel as number);

  const bend = Math.abs(curvature);
  top = Math.min(top, (cfg.maxVel * cfg.velMargin) / (1 + (bend * cfg.trackwidth) / 2));

  return top;
}

function sampleSpline(path: BezierSegment[], cfg: ProfileConfig): ProfileSample[] {
  const samples: ProfileSample[] = [];

  for (let i = 0; i < path.length; i++) {
    const seg = path[i];

    const length = bezierLength(seg, 100);
    const count = sampleCount(length, cfg.spacing);
    const stride = length / count;
    const params = arcSpacedParams(seg, count);

    // Every segment samples its own t = 0, so a joint carries TWO rows at the
    // same place and the same timestamp: the outgoing segment's curvature and
    // the incoming one's. Handle-based splines are only C1.
    for (let j = 0; j <= count; j++) {
      const t = params[j];
      const p = bezierAt(seg, t);
      const curvature = bezierCurvature(seg, t);
      samples.push({
        x: p.x,
        y: p.y,
        heading: bezierHeading(seg, t) + (seg.reversed ? 180 : 0),
        curvature,
        accel: (seg.maxAccel ?? -1) > 0 ? (seg.maxAccel as number) : cfg.maxAccel,
        ceiling: speedCeiling(curvature, seg, cfg),
        reversed: !!seg.reversed,
        step: j === 0 ? 0 : stride
      });
    }

    // A cusp: the path doubles back, and the robot cannot flip direction while
    // moving. Pinning the ceiling to 0 is all it takes.
    const flips = i + 1 < path.length && !!path[i + 1].reversed !== !!seg.reversed;
    if (seg.stopEnd || flips) samples[samples.length - 1].ceiling = 0;
  }
  return samples;
}

/** Bakes a spline into a schedule (profile.cpp bakeTrajectory). */
export function bakeTrajectory(
  path: BezierSegment[],
  config: Partial<ProfileConfig>,
  rc: RamseteConfig
): TrajPoint[] {
  const traj: TrajPoint[] = [];
  if (!path.length) return traj;

  const cfg = resolveProfile(config, rc);
  const samples = sampleSpline(path, cfg);

  const count = samples.length;
  if (count < 2) return traj;

  const step = new Array<number>(count).fill(0);
  for (let i = 1; i < count; i++) step[i] = samples[i].step;

  // Ceiling 3, forward: how fast can I be here given how fast I was there, and
  // given how much torque the motors still have at that speed.
  const speed = new Array<number>(count).fill(0);
  speed[0] = Math.min(cfg.startSpeed, samples[0].ceiling);
  for (let i = 1; i < count; i++) {
    const accel = samples[i].accel * torqueFraction(speed[i - 1] / Math.max(cfg.maxVel, 1e-6), cfg);
    const grown = Math.sqrt(speed[i - 1] * speed[i - 1] + 2 * accel * step[i]);
    speed[i] = Math.min(samples[i].ceiling, grown);
  }

  // Ceiling 3, backward: every decel ramp in the final profile comes from this.
  const finalSpeed = Math.min(cfg.endSpeed, samples[count - 1].ceiling);
  speed[count - 1] = Math.min(speed[count - 1], finalSpeed);
  for (let i = count - 2; i >= 0; i--) {
    const braked = Math.sqrt(speed[i + 1] * speed[i + 1] + 2 * cfg.maxDecel * step[i + 1]);
    speed[i] = Math.min(speed[i], braked);
  }

  // Time integration: ramps contribute dv/a, cruises contribute dd/v.
  let clock = 0;
  for (let i = 0; i < count; i++) {
    if (i > 0 && step[i] > 1e-9) {
      const accel = (speed[i] * speed[i] - speed[i - 1] * speed[i - 1]) / (2 * step[i]);
      if (Math.abs(accel) > 0.1) clock += (speed[i] - speed[i - 1]) / accel;
      else clock += step[i] / Math.max(speed[i], MIN_INTEGRATION_SPEED);
    }

    traj.push({
      t: clock,
      x: samples[i].x,
      y: samples[i].y,
      heading: wrapDegrees(samples[i].heading),
      v: samples[i].reversed ? -speed[i] : speed[i],
      // omega is derived, never authored, so it cannot drift out of agreement
      // with the curve being traced.
      omega: speed[i] * samples[i].curvature * R2D
    });
  }
  return traj;
}

// =========================================================================
// ramseteController — the tracking law and the adaptive clock.
//
// sim.ts's runTrajectory calls the five functions below every tick
// (error/gain/clockRate/targetV/targetOmega), against a PERFECT actuator —
// see that function's own doc comment. VelocityController further down is
// still unreferenced: a perfect actuator has no velocity loop to run. Keep
// both sections exact regardless (see the file header) — do not let the
// unused one drift from the C++ just because nothing currently calls it.
// =========================================================================

/** The pose error, seen from the robot's seat. */
export interface RamseteError {
  forward: number; // in — how far ahead the reference is along the nose
  crossTrack: number; // in — how far to the robot's RIGHT it is
  heading: number; // deg — wrapped heading gap
}

/** The gap between odometry and the schedule, rotated into the robot's own axes. */
export function ramseteError(ref: TrajState, robot: { x: number; y: number; h: number }): RamseteError {
  const dx = ref.x - robot.x;
  const dy = ref.y - robot.y;
  const h = robot.h * D2R;
  return {
    forward: dx * Math.sin(h) + dy * Math.cos(h),
    crossTrack: dx * Math.cos(h) - dy * Math.sin(h),
    heading: wrapDegrees(ref.heading - robot.h)
  };
}

/** The scheduled gain, shared by every feedback term. */
export function ramseteGain(ref: TrajState, rc: RamseteConfig): number {
  const omega = ref.omega * D2R;
  const speed = Math.max(Math.abs(ref.v), Math.abs(rc.vMinGain));
  return 2 * rc.zeta * Math.sqrt(omega * omega + rc.b * speed * speed);
}

/**
 * Reference-clock throttle, in roughly [0, 1]: the facing factor |cos(e_heading)|,
 * minus a brake that engages only once the law's catch-up demand outruns the
 * robot's spare speed.
 */
export function ramseteClockRate(e: RamseteError, ref: TrajState, k: number, rc: RamseteConfig): number {
  const facing = Math.abs(Math.cos(e.heading * D2R));

  // On a reversed row the reference sits behind the nose, so `forward` goes
  // negative for the same lag — the direction sign puts it back.
  const direction = ref.v < 0 ? -1 : 1;
  const demand = direction * k * e.forward;

  const headroom = Math.max(rc.maxVel - Math.abs(ref.v), 0);
  const denom = Math.max(k * Math.abs(ref.v) * facing, 1);

  const brake = rc.clockCoupling * Math.min((headroom - demand) / denom, 0);

  return Math.max(facing + brake, 0);
}

/** The forward half of the law. */
export function ramseteTargetV(ref: TrajState, e: RamseteError, k: number): number {
  return ref.v * Math.cos(e.heading * D2R) + k * e.forward;
}

/** The steering half; a braked clock slows the scheduled turning too. */
export function ramseteTargetOmega(
  ref: TrajState,
  e: RamseteError,
  k: number,
  clockRate: number,
  rc: RamseteConfig
): number {
  const heading = e.heading * D2R;
  const omega =
    ref.omega * D2R * clockRate + k * heading + rc.b * ref.v * sinc(heading) * e.crossTrack;
  return omega * R2D;
}

// =========================================================================
// moveTo — the same law aimed at a fixed point. Mirrors Ramsete::moveToError and
// Ramsete::moveToTurn in src/ramsete/ramseteController.cpp.
// =========================================================================

/**
 * moveTo's error, measured from the DIRECTION OF TRAVEL rather than the nose.
 * Two states, not three: crossTrack is distance·sin(heading) by construction.
 */
export interface MoveToError {
  heading: number; // deg — where the target sits off the direction of travel. + is right.
  distance: number; // in — signed. Negative driving backward, and negative once overshot.
  crossTrack: number; // in — lateral component, sign paired with distance
}

export function moveToError(
  targetX: number,
  targetY: number,
  robot: { x: number; y: number; h: number },
  driveBackward: boolean
): MoveToError {
  const dx = targetX - robot.x;
  const dy = targetY - robot.y;

  // flipped 180° for a backward move, so this is measured from the direction of TRAVEL
  let travel = Math.atan2(dx, dy) * R2D;
  if (driveBackward) travel += 180;

  const heading = wrapDegrees(travel - robot.h);
  let distance = Math.hypot(dx, dy);
  if (driveBackward) distance = -distance;

  // SIGNED, not the magnitude. Driving tail-first reverses the drive command AND
  // swaps left/right; both flips live in this sign and cancel inside moveToTurn's
  // v·e_y product. Using the magnitude inverts every backward move.
  return { heading, distance, crossTrack: distance * Math.sin(heading * D2R) };
}

/** kLat·v·e_y·sinc(e_θ), in VOLTS. drivePower is the cosine-scaled command. */
export function moveToTurn(e: MoveToError, drivePower: number, kLat: number): number {
  return kLat * drivePower * sinc(e.heading * D2R) * e.crossTrack;
}

// =========================================================================
// velocityController — one channel of the velocity layer (mirrors the PID class).
// =========================================================================

export interface VelocityGains {
  kV: number;
  kA: number;
  kS: number;
  kP: number;
  kI: number;
  /** cap on the accumulated integral term, volts */
  maxIntegralOutput: number;
}

const sign = (v: number) => (v > 0 ? 1 : v < 0 ? -1 : 0);

/**
 * calculateOutput(target, current, dt). The library reads `current` through a
 * stored pointer and takes dt off the system clock; the sandbox hands both in,
 * since its clock is the fixed 10 ms sim tick.
 */
export class VelocityController {
  gains: VelocityGains;
  error = 0;
  output = 0;
  private sumError = 0;
  private lastTarget = 0;
  private firstCycle = true;

  constructor(gains: VelocityGains) {
    this.gains = gains;
  }

  clearAllValues() {
    this.error = 0;
    this.output = 0;
    this.sumError = 0;
    this.lastTarget = 0;
    this.firstCycle = true;
  }

  calculateOutput(target: number, current: number, dt: number): number {
    let step = dt;
    // The first tick after a reset has no previous command to difference
    // against, so it reports no acceleration rather than an infinite step.
    if (this.firstCycle) {
      this.lastTarget = target;
      step = 0;
      this.firstCycle = false;
    }

    // Acceleration is the finite difference of consecutive COMMANDS, never of
    // measurements: feedforward should deliver what the plan asked for.
    const accel = step > 1e-6 ? (target - this.lastTarget) / step : 0;
    this.lastTarget = target;

    const g = this.gains;
    this.error = target - current;

    this.sumError += g.kI * this.error * step;
    const cap = Math.abs(g.maxIntegralOutput);
    this.sumError = Math.max(-cap, Math.min(cap, this.sumError));

    this.output = g.kV * target + g.kS * sign(target) + g.kA * accel + g.kP * this.error + this.sumError;
    return this.output;
  }
}
