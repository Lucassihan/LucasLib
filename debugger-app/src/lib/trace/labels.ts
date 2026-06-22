// Human-readable movement names + type categories.
// (Design spec: show "Drive Forward", not "driveForward".)

const PRETTY: Record<string, string> = {
  face: 'Face',
  facePoint: 'Face Point',
  swingOnLeft: 'Swing Left',
  swingOnRight: 'Swing Right',
  swingOnLeftToPoint: 'Swing Left to Point',
  swingOnRightToPoint: 'Swing Right to Point',
  driveForward: 'Drive Forward',
  driveBackward: 'Drive Backward',
  moveTo: 'Move To',
  boomerangTo: 'Boomerang',
  lyPose: 'lyPose'
};

export function prettyMovement(type: string): string {
  return PRETTY[type] ?? type;
}

const TURNS = new Set([
  'face',
  'facePoint',
  'swingOnLeft',
  'swingOnRight',
  'swingOnLeftToPoint',
  'swingOnRightToPoint'
]);

/** Turn-style movement (rotation) vs lateral-style (driving). */
export function isTurn(type: string): boolean {
  return TURNS.has(type);
}

// Movements whose (targetX, targetY) is where the robot is supposed to END
// (so a field waypoint there is meaningful). Excludes facePoint / swing-to-point
// — those take an (x, y) but only FACE it; the robot ends elsewhere.
// lyPose marks its own target in LyPoseOverlay, so it doesn't get a Waypoint.
const ENDS_AT_TARGET = new Set(['moveTo', 'boomerangTo']);

export function endsAtTarget(type: string): boolean {
  return ENDS_AT_TARGET.has(type);
}

/** lyPose — path-follow Lyapunov drive (gets the Lyapunov readout + overlay). */
export function isLyPose(type: string): boolean {
  return type === 'lyPose';
}

// Vibrant palette, by movement type:
//   face → red, swing → orange, drive fwd/bwd → blue, moveTo → green, boomerang → yellow
const COLORS: Record<string, string> = {
  face: '#ef4444',
  facePoint: '#ef4444',
  swingOnLeft: '#f97316',
  swingOnRight: '#f97316',
  swingOnLeftToPoint: '#f97316',
  swingOnRightToPoint: '#f97316',
  driveForward: '#3b82f6',
  driveBackward: '#3b82f6',
  moveTo: '#22c55e',
  boomerangTo: '#eab308',
  lyPose: '#2dd4bf'
};

export function movementColor(type: string): string {
  return COLORS[type] ?? '#888888';
}

// PID term colors — consistent across the readout bars and the breakdown graph
// lines. P = red, I = white, D = blue. Channel identity (Drive vs Turn) is
// carried by the panel title/border, so term color never clashes with it.
export const PID_TERM = { p: '#ef4444', i: '#e8e8e8', d: '#3b82f6' };
