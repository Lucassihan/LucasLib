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

// Which governor stage a view is looking at — the combined TotalOutput, or one
// of the two PID limiters. (Mirrors the identical literal union in
// $lib/stores/settings.svelte, which owns the UI's own selected-scope state;
// this copy is what TraceHeader.govern and the trace-side helpers key on.)
export type GovernScope = 'total' | 'drive' | 'turn';

export interface TraceHeader {
  version: string;
  autonName: string;
  startPose: { x: number; y: number; h: number };
  batteryPct: number; // battery % at debugger::start(); -1 = not in this trace
  batteryVolts: number; // battery volts at start; 0 = not in this trace
  trackwidth: number;
  headingDisableDist: number;
  kLat: number; // ramsete.moveTo cross-track coupling, 1/in; -1 = not in this trace
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
  | 'drive'
  | 'moveTo'
  | 'boomerangTo'
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
  pathHeading: number; // unused since lyPose was removed; kept so old traces still parse
  turnDisableLength: number; // unused since lyPose was removed; kept so old traces still parse
  leftVolts: number; // voldrive: commanded left-side volts (open loop)
  rightVolts: number; // voldrive: commanded right-side volts (open loop)
  heading: number;
  cosineScaling: boolean;
  turnPriority: number;
  exitTimeout: number;
  // NOTE: these five keep their original column names so old traces still parse,
  // but three of them carry a different quantity since the EC overhaul. -1 is the
  // off sentinel on all of them now (it used to be 0, or negative for the cross).
  //
  // settleD: carries ec.v — the settle SPEED threshold. Settle now needs the chassis
  // to be slowing down AND inside exitDist AND under this speed. in/s on a lateral,
  // rad/s on a turn. It is NOT the PID D term it used to be.
  settleD: number;
  // exitDist: carries ec.settleWithin — the SETTLE activation zone on BOTH channels,
  // inches on a lateral, DEGREES on a turn.
  exitDist: number;
  // exitPerpLine: carries ec.d on a lateral. The line is infinite, runs ⊥ to the
  // robot's heading through the target, and fires on which SIDE of it we're on — d
  // slides it that many inches back toward the robot to fire early. 0 = the line sits
  // on the target. driveForward()/driveBackward() have no point target, so cross is
  // skipped there entirely and d acts as a plain remaining-distance tolerance instead.
  exitPerpLine: number;
  // exitAng: turns only — carries ec.d, the ungated ANGLE exit in deg. Fires
  // mid-rotation as soon as |error| <= d, without waiting for the robot to slow.
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
  refX: number;   // ramsete.trajectory: the pose the schedule wanted this tick
  refY: number;
  refH: number;
  cmdV: number;   // what the law asked the velocity layer for, in/s
  cmdW: number;   // …and deg/s
  errFwd: number; // robot-frame gap to the reference: forward, right, heading
  errCross: number;
  errHead: number;
  clockRate: number; // the adaptive clock's throttle on reference time
  v: number;      // measured chassis speed, in/s
  w: number;      // measured chassis turn rate, deg/s
  arcRadius: number; // travelArcRadius() — instantaneous turn radius (in)
  maxSlip: number; // slip cap √(leadChasePowerLimit·|r|); 0 = inactive
  // ramsete.moveTo's MoveToError.crossTrack (in), sampled in the control loop.
  // NaN on traces recorded before the column existed — see crossTrackAt().
  crossTrack: number;
  driveI: number; // drive channel integral (∫ lateral error)
  driveD: number; // drive channel derivative
  turnI: number; // turn/heading channel integral
  turnD: number; // turn/heading channel derivative
  bat: number; // battery % this tick; -1 = not in this trace
  mtemp: number; // avg drive-motor temp (°C) this tick; -1 = not in this trace
}

// One baked row of a ramsete.trajectory's schedule — LucasLib's TrajPoint, as
// emitted in the trace's [SCHEDULE] section.
//
// This is the PLAN, and it is not recoverable from the ticks: rows are spaced by
// ARC (~2 in apart), they all exist before the first tick runs, and the per-tick
// refX/refY only ever reach as far as the reference clock actually got.
export interface ScheduleRow {
  t: number; // ms into the schedule's OWN clock — not run time
  x: number;
  y: number;
  h: number; // deg, the tangent (flipped 180° on a reversed row)
  v: number; // in/s, signed: negative is driven backward
  w: number; // deg/s
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
  /** Baked schedules keyed by movement index; empty for traces without [SCHEDULE]. */
  schedules: Record<number, ScheduleRow[]>;
  /** ms — end of the run (last tick timestamp). */
  duration: number;
}
