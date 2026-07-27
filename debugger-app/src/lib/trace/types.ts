// Typed model of a LucasLib trace.
// Mirrors LucasLib Notebook/Debugger App/Data Export Format — spec.

// Output-governor settings (one set per scope). Mirrors LucasLib's TotalOutput
// struct and the per-PID limits: floor, cap, and ramp rates (V/tick; 0 = off).
export interface GovernConfig {
  minOutput: number;
  maxOutput: number;
  maxAccelRate: number;
  maxDecelRate: number;
}

export interface TraceHeader {
  version: string;
  autonName: string;
  startPose: { x: number; y: number; h: number };
  batteryPct: number; // battery % at debugger::start(); -1 = not in this trace
  batteryVolts: number; // battery volts at start; 0 = not in this trace
  trackwidth: number;
  headingDisableDist: number;
  useLyapunov: boolean;
  kLat: number;
  govern: {
    total: GovernConfig;
    drive: GovernConfig;
    turn: GovernConfig;
  };
}

export type MovementType =
  | 'face'
  | 'facePoint'
  | 'swingOnLeft'
  | 'swingOnRight'
  | 'swingOnLeftToPoint'
  | 'swingOnRightToPoint'
  | 'driveForward'
  | 'driveBackward'
  | 'moveTo'
  | 'boomerangTo'
  | 'lyPose'
  | 'voldrive';

export interface Movement {
  index: number;
  type: MovementType | string;
  targetX: number;
  targetY: number;
  targetH: number;
  distance: number;
  backwards: boolean;
  direction: number;
  partiality: number;
  dLead: number;
  n: number;
  gLead: number; // ghost lead [0,1]
  leadChasePowerLimit: number; // centripetal slip cap grip constant; <=0 disabled
  pathHeading: number; // lyPose: fixed line heading (deg) the robot rides
  turnDisableLength: number; // lyPose: within this dist (in) the turn is disabled
  leftVolts: number; // voldrive: commanded left-side volts (open loop)
  rightVolts: number; // voldrive: commanded right-side volts (open loop)
  heading: number;
  cosineScaling: boolean;
  turnPriority: number;
  exitTimeout: number;
  // settleD: settle is derivative-magnitude based — once inside exitDist, the move
  // waits until |D term| < settleD before exiting. It is a THRESHOLD, not a switch:
  // 0 makes settle unreachable (exitDist = 0 is the off switch). Units are Δerror
  // per MILLISECOND, so the lib's default of 2 never binds at V5 speeds.
  settleD: number;
  // exitDist: the SETTLE activation zone (ec.setR) on BOTH channels — inches on a
  // lateral, DEGREES on a turn. 0 = settle off.
  exitDist: number;
  // exitPerpLine: the CROSS offset (ec.crossD), in. The line is infinite, runs ⊥ to
  // the robot's heading through the target, and fires on which SIDE of it we're on —
  // crossD slides it that many inches back toward the robot to fire early. 0 = the
  // line sits on the target; NEGATIVE = cross is off (so test >= 0, not > 0).
  exitPerpLine: number;
  // exitAng: turns only — the ANGLE exit (ec.extA), deg. Independent of settle and
  // ungated, so it fires mid-rotation as soon as |error| <= extA. 0 = off.
  exitAng: number;
  exitReason: string;
  startTime: number;
  endTime: number;
  // locked PID constants for this movement (drive channel + turn/heading channel)
  driveKp: number;
  driveKi: number;
  driveKd: number;
  turnKp: number;
  turnKi: number;
  turnKd: number;
}

// Readable field names; CSV short names are mapped in the parser.
export interface Tick {
  t: number; // ms
  mi: number; // movement index
  x: number;
  y: number;
  h: number;
  stage: number;
  lateralError: number; // le
  headingError: number; // he
  drivePow: number; // dp
  headingPow: number; // hp
  turnPow: number; // tp
  swingPow: number; // sp
  driveOut: number; // do
  turnOut: number; // to
  leftOut: number; // lo
  rightOut: number; // ro
  yInches: number; // yi
  xInches: number; // xi
  leadX: number;
  leadY: number;
  leadRetraction: number;
  perpDist: number;
  // New ghost-point boomerang geometry (per tick)
  carrotX: number; // live (retracting) lead point
  carrotY: number;
  ghostX: number; // ghost trailing behind the carrot
  ghostY: number;
  arcRadius: number; // travelArcRadius() — instantaneous turn radius (in)
  maxSlip: number; // slip cap √(leadChasePowerLimit·|r|); 0 = inactive
  ex: number;
  ey: number;
  driveI: number; // drive channel integral (∫ lateral error)
  driveD: number; // drive channel derivative
  turnI: number; // turn/heading channel integral
  turnD: number; // turn/heading channel derivative
  bat: number; // battery % this tick; -1 = not in this trace
  mtemp: number; // avg drive-motor temp (°C) this tick; -1 = not in this trace
}

// Timeline annotation — a strategy/action callout pinned to a moment in the
// run ("intake pin", "score S red goal", "flip cup clear-side down", …).
// Emitted by debugger::note("...") on the robot or by the sample generator.
export interface TraceNote {
  t: number; // ms
  mi: number; // movement index active when the note fired; -1 = none
  text: string;
}

export interface ReadWall {
  t: number;
  mi: number;
  wall: number;
  sensor: number;
  preX: number;
  preY: number;
  preH: number;
  postX: number;
  postY: number;
  postH: number;
}

export interface Trace {
  header: TraceHeader;
  movements: Movement[];
  ticks: Tick[];
  readWalls: ReadWall[];
  notes: TraceNote[];
  /** ms — end of the run (last tick timestamp). */
  duration: number;
}
