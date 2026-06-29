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
  // settle is now derivative-magnitude based: once inside the exit zone
  // (exitDist / exitAng), the move waits until |D term| < settleD before exiting.
  // 0 = no settle gate (exit as soon as the zone is reached).
  settleD: number;
  exitDist: number;
  exitPerpLine: number;
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
  /** ms — end of the run (last tick timestamp). */
  duration: number;
}
