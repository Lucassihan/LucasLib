// The LucasLib movement API, as the sandbox knows it. Signatures mirror
// include/movement/movement.h EXACTLY — code written in the sandbox text box
// must compile unchanged when pasted into an auton.
//
// `kind` drives both the parser's argument checking and the editor's
// signature-hint card.

export type ArgKind =
  | 'number' // double
  | 'int' // int (direction)
  | 'bool'
  | 'lateralEc' // LateralExitConditions {...}
  | 'turnEc' // TurnExitConditions {...}
  | 'blend' // BlendSettings {...}
  | 'boomParams' // BoomParams variable (or inline {...})
  | 'bezierPath' // std::vector<BezierSegment> variable
  | 'headingDefault'; // double heading with `chassis.h` default sentinel

export interface ParamSpec {
  name: string;
  kind: ArgKind;
  /** C++ default-value text; undefined = required */
  def?: string;
  doc: string;
}

export interface FnSpec {
  name: string;
  params: ParamSpec[];
  doc: string;
}

export interface StructFieldSpec {
  name: string;
  /** 'point' is a nested `{x, y}` pair — only BezierSegment's control points use it. */
  kind: 'number' | 'bool' | 'point';
  def: number | boolean;
  doc: string;
}

// -1 is the OFF sentinel on every field of both structs. Settle is now speed-based:
// it needs the chassis to be slowing down AND inside settleWithin AND under the speed
// threshold `v` — it is no longer a PID-derivative test.
// v = 2 in/s and 0.35 rad/s mirror exitConditions.h's LateralExitConditions /
// TurnExitConditions defaults — the old 10/10 values were nowhere near a real
// stop (10 in/s is ~18% of this drivetrain's 55 in/s top speed; 10 rad/s is
// 573 deg/s, faster than a turn can ever spin), so the settle gate either
// exited early (lateral) or never gated at all (turn). See exitConditions.h.
export const LATERAL_EC: StructFieldSpec[] = [
  { name: 't', kind: 'number', def: 5000, doc: 'timeout ms · -1 = off' },
  { name: 'v', kind: 'number', def: 2, doc: 'settle speed threshold, in/s — exit once the chassis is slowing AND |v| is under this, inside settleWithin · -1 = off' },
  { name: 'd', kind: 'number', def: -1, doc: 'cross: slide the ⊥ line this many in TOWARD us to fire early · 0 = line on the target · -1 = off. On driveForward()/driveBackward() there is no point target, so this acts as a plain remaining-distance tolerance instead.' },
  { name: 'settleWithin', kind: 'number', def: 12, doc: 'settle activation radius, in — settle only counts inside this' }
];

export const TURN_EC: StructFieldSpec[] = [
  { name: 't', kind: 'number', def: 5000, doc: 'timeout ms · -1 = off' },
  { name: 'v', kind: 'number', def: 0.35, doc: 'settle angular-speed threshold, rad/s — exit once |ω| is falling AND under this, inside settleWithin · -1 = off' },
  { name: 'd', kind: 'number', def: -1, doc: 'ungated exit angle from target, deg — fires mid-turn, no settle gate · -1 = off' },
  { name: 'settleWithin', kind: 'number', def: 20, doc: 'settle activation angle, deg — settle only counts inside this' }
];

export const BLEND: StructFieldSpec[] = [
  { name: 'cosineScaling', kind: 'bool', def: true, doc: 'drive × cos(headingError) before blending' },
  { name: 'turnPriority', kind: 'number', def: 1, doc: '[0,1] · 1 = drive absorbs the whole cut' }
];

// BezierSegment (ramsete/bezierTools.h) — one cubic segment of a path. Only
// the control points' x/y matter; a control point has no facing.
export const BEZIER_SEG: StructFieldSpec[] = [
  { name: 'a', kind: 'point', def: 0, doc: 'segment start, in' },
  { name: 'b', kind: 'point', def: 0, doc: 'first handle, in' },
  { name: 'c', kind: 'point', def: 0, doc: 'second handle, in' },
  { name: 'd', kind: 'point', def: 0, doc: 'segment end, in' },
  { name: 'reversed', kind: 'bool', def: false, doc: 'drive this piece backward (heading flips 180°, v goes negative)' },
  { name: 'stopEnd', kind: 'bool', def: false, doc: 'force a full stop at this segment’s end' },
  { name: 'maxVel', kind: 'number', def: -1, doc: 'in/s ceiling for this segment · -1 = profile default' },
  { name: 'maxAccel', kind: 'number', def: -1, doc: 'in/s² budget for this segment · -1 = profile default' }
];

export const BOOM_PARAMS: StructFieldSpec[] = [
  { name: 'x', kind: 'number', def: 0, doc: 'target x, in' },
  { name: 'y', kind: 'number', def: 0, doc: 'target y, in' },
  { name: 'heading', kind: 'number', def: 0, doc: 'final heading, deg' },
  { name: 'lead', kind: 'number', def: 0.5, doc: 'carrot lead — coefficient of initial robot→target dist' },
  { name: 'gLead', kind: 'number', def: 0, doc: 'ghost lead [0,1] — how far the ghost trails the carrot' },
  { name: 'leadChasePowerLimit', kind: 'number', def: -1, doc: 'centripetal slip cap (grip const) · -1 = off' }
];

// Assignable PID config fields — real members of the LucasLib PID class,
// written as statements like `drivePID.maxOutput = 10;`. targetOffset is
// per-movement (the movement zeroes it on exit); the rest persist.
export const PID_FIELDS: StructFieldSpec[] = [
  { name: 'targetOffset', kind: 'number', def: 0, doc: 'bias the NEXT movement: settle N past the target (drive) / N deg past (turn); auto-zeroed after' },
  { name: 'maxOutput', kind: 'number', def: 12, doc: 'output cap, V' },
  { name: 'minOutput', kind: 'number', def: 0, doc: 'output floor, V · 0 = off' },
  { name: 'maxAccelRate', kind: 'number', def: 0.6, doc: 'slew up, V/tick · 0 = off' },
  { name: 'maxDecelRate', kind: 'number', def: 1.2, doc: 'slew down, V/tick · 0 = off' },
  { name: 'integralBound', kind: 'number', def: 0, doc: 'error band where I accumulates' },
  { name: 'maxIntegralOutput', kind: 'number', def: 0, doc: 'cap on the I term' }
];

// Stage-2 output governor (LucasLib TotalOutput) — assignable like the PIDs,
// e.g. `totalOutput.maxOutput = 10;`. Applies to the blended left/right sides.
export const TOTAL_FIELDS: StructFieldSpec[] = [
  { name: 'minOutput', kind: 'number', def: 0, doc: 'per-side floor when nonzero, V · 0 = off' },
  { name: 'maxOutput', kind: 'number', def: 12, doc: 'ratio-preserving cap on the sides, V' },
  { name: 'maxAccelRate', kind: 'number', def: 0, doc: 'per-side slew up, V/tick · 0 = unlimited' },
  { name: 'maxDecelRate', kind: 'number', def: 0, doc: 'per-side slew down, V/tick · 0 = unlimited' }
];

// Docs for bare global assignments.
export const GLOBAL_ASSIGNS: Record<string, string> = {
  backward: 'Global drive direction: every point/pose movement (facePoint, swings-to-point, moveTo, boomerangTo) runs backward while true. Starts FALSE; nothing resets it — flip it yourself. Absolute-heading movements (face, swings-to-heading) and driveForward()/driveBackward() ignore it entirely — those two take their direction from which function you call, not from this global.'
};

export const STRUCT_FIELDS: Record<string, StructFieldSpec[]> = {
  lateralEc: LATERAL_EC,
  turnEc: TURN_EC,
  blend: BLEND,
  boomParams: BOOM_PARAMS,
  bezierSeg: BEZIER_SEG
};

export const STRUCT_NAMES: Record<string, string> = {
  lateralEc: 'LateralExitConditions',
  turnEc: 'TurnExitConditions',
  blend: 'BlendSettings',
  boomParams: 'BoomParams',
  bezierSeg: 'BezierSegment',
  bezierPath: 'std::vector<BezierSegment>'
};

const ecL: ParamSpec = { name: 'ec', kind: 'lateralEc', def: '{}', doc: 'exit conditions' };
const ecT: ParamSpec = { name: 'ec', kind: 'turnEc', def: '{}', doc: 'exit conditions' };
const bs: ParamSpec = { name: 'bs', kind: 'blend', def: '{}', doc: 'output blending' };
const dir: ParamSpec = { name: 'direction', kind: 'int', def: '0', doc: '0 = shortest · 1 = CW · -1 = CCW' };
const part: ParamSpec = { name: 'partiality', kind: 'number', def: '0', doc: 'locked-side power ratio · −1 = spot turn · 0 = swing (half speed) · +1 = straight' };

export const FUNCTIONS: FnSpec[] = [
  {
    name: 'face',
    doc: 'Spot-turn to a heading.',
    params: [{ name: 'heading', kind: 'number', doc: 'target heading, deg' }, ecT, dir]
  },
  {
    name: 'facePoint',
    doc: 'Spot-turn to face a point — or facePoint(bp): face a BoomParams target\u2019s lead point (honors the global backward).',
    params: [
      { name: 'x', kind: 'number', doc: 'point x, in · or pass a BoomParams name' },
      { name: 'y', kind: 'number', doc: 'point y, in' },
      ecT,
      dir
    ]
  },
  {
    name: 'swingOnLeft',
    doc: 'Swing turn about the left side to a heading.',
    params: [{ name: 'heading', kind: 'number', doc: 'target heading, deg' }, ecT, part, dir]
  },
  {
    name: 'swingOnRight',
    doc: 'Swing turn about the right side to a heading.',
    params: [{ name: 'heading', kind: 'number', doc: 'target heading, deg' }, ecT, part, dir]
  },
  {
    name: 'swingOnLeftToPoint',
    doc: 'Swing about the left side to face a point.',
    params: [
      { name: 'x', kind: 'number', doc: 'point x, in' },
      { name: 'y', kind: 'number', doc: 'point y, in' },
      ecT,
      part,
      dir
    ]
  },
  {
    name: 'swingOnRightToPoint',
    doc: 'Swing about the right side to face a point.',
    params: [
      { name: 'x', kind: 'number', doc: 'point x, in' },
      { name: 'y', kind: 'number', doc: 'point y, in' },
      ecT,
      part,
      dir
    ]
  },
  {
    name: 'driveForward',
    doc: 'Relative straight drive along a held heading, forward. distance is a POSITIVE magnitude. Ignores the global backward entirely.',
    params: [
      { name: 'distance', kind: 'number', doc: 'inches, positive magnitude' },
      { name: 'heading', kind: 'headingDefault', def: 'chassis.h', doc: 'held heading (nose, either direction) · default = heading at call' },
      ecL,
      bs
    ]
  },
  {
    name: 'driveBackward',
    doc: 'Relative straight drive along a held heading, in reverse. distance is a POSITIVE magnitude — driveBackward(12) reverses twelve inches. Ignores the global backward entirely.',
    params: [
      { name: 'distance', kind: 'number', doc: 'inches, positive magnitude' },
      { name: 'heading', kind: 'headingDefault', def: 'chassis.h', doc: 'held heading (nose, either direction) · default = heading at call' },
      ecL,
      bs
    ]
  },
  {
    name: 'moveTo',
    doc: 'Point-to-point drive; heading correction bends the path in. Honors the global backward.',
    params: [
      { name: 'x', kind: 'number', doc: 'target x, in' },
      { name: 'y', kind: 'number', doc: 'target y, in' },
      ecL,
      bs
    ]
  },
  {
    name: 'ramsete.moveTo',
    doc: 'Lyapunov point drive — RAMSETE\'s law with the schedule taken away. Chases the live bearing to the target and adds a cross-track term that bends the path in, scaled by how much travel is left. Backs up on its own once the target is behind it. Honors the global backward. No BlendSettings: the cosine scaling is part of the law.',
    params: [
      { name: 'x', kind: 'number', doc: 'target x, in' },
      { name: 'y', kind: 'number', doc: 'target y, in' },
      ecL
    ]
  },
  {
    name: 'boomerangTo',
    doc: 'Three-stage ghost-point boomerang to a pose (uses a BoomParams).',
    params: [{ name: 'bp', kind: 'boomParams', doc: 'BoomParams variable' }, ecL]
  },
  {
    name: 'voldrive',
    doc: 'Open-loop volts: (left, right) or one value for both. Runs until the next wait() elapses.',
    params: [
      { name: 'left', kind: 'number', doc: 'volts ±12' },
      { name: 'right', kind: 'number', def: '(left)', doc: 'volts ±12 · omit to use left for both' }
    ]
  },
  {
    name: 'wait',
    doc: 'VEXcode wait — holds the current voldrive (or pauses) for a duration.',
    params: [
      { name: 'time', kind: 'number', doc: 'duration' },
      { name: 'units', kind: 'int', def: 'msec', doc: 'msec / sec' }
    ]
  },
  {
    name: 'delay',
    doc: 'Pause for a duration in milliseconds — holds any active voldrive (like wait(ms, msec)).',
    params: [{ name: 'ms', kind: 'number', doc: 'milliseconds' }]
  },
  {
    name: 'chassis.setPos',
    doc: 'Seed the odometry pose. Every auton starts with one.',
    params: [
      { name: 'x', kind: 'number', doc: 'inches' },
      { name: 'y', kind: 'number', doc: 'inches' },
      { name: 'h', kind: 'number', doc: 'heading, deg' }
    ]
  },
  {
    name: 'ramsete.trajectory',
    doc: 'Bake a spline and follow it with the RAMSETE law; the schedule is the finish line.',
    params: [
      { name: 'path', kind: 'bezierPath', doc: 'a std::vector<BezierSegment> declared above' },
      { name: 'ec', kind: 'lateralEc', def: '{.t = 10000, .v = -1}', doc: 'exit conditions — the safety bound on top of the schedule' }
    ]
  }
];

export const FN_BY_NAME: Record<string, FnSpec> = Object.fromEntries(FUNCTIONS.map((f) => [f.name, f]));

/** Pretty one-line signature for the hint card, e.g. from the header file. */
export function signatureOf(fn: FnSpec): string {
  const ps = fn.params
    .map((p) => {
      const type =
        p.kind === 'number' || p.kind === 'headingDefault'
          ? 'double'
          : p.kind === 'int'
            ? 'int'
            : p.kind === 'bool'
              ? 'bool'
              : STRUCT_NAMES[p.kind] ?? p.kind;
      return `${type} ${p.name}${p.def !== undefined ? ` = ${p.def}` : ''}`;
    })
    .join(', ');
  return `${fn.name}(${ps})`;
}
