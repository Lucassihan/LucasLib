// Human-readable movement names + type categories.
// (Design spec: show "Move To", not "moveTo".)

const PRETTY: Record<string, string> = {
  face: 'Face',
  facePoint: 'Face Point',
  swingOnLeft: 'Swing Left',
  swingOnRight: 'Swing Right',
  swingOnLeftToPoint: 'Swing Left to Point',
  swingOnRightToPoint: 'Swing Right to Point',
  drive: 'Drive',
  moveTo: 'Move To',
  boomerangTo: 'Boomerang',
  voldrive: 'Voltage Drive',
  // retired — kept so archived traces still render with a readable name
  driveForward: 'Drive Forward',
  driveBackward: 'Drive Backward',
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
const ENDS_AT_TARGET = new Set(['moveTo', 'boomerangTo']);

export function endsAtTarget(type: string): boolean {
  return ENDS_AT_TARGET.has(type);
}

/** voldrive — open-loop voltage command (no PID, no exit, no governing). */
export function isVolDrive(type: string): boolean {
  return type === 'voldrive';
}

const MOVEMENT_COLORS: Record<string, string> = {
  face: '#ef4444',
  facePoint: '#ef4444',
  swingOnLeft: '#f97316',
  swingOnRight: '#f97316',
  swingOnLeftToPoint: '#f97316',
  swingOnRightToPoint: '#f97316',
  drive: '#3b82f6',
  moveTo: '#22c55e',
  boomerangTo: '#eab308',
  voldrive: '#a855f7',
  // retired — archived traces keep their original colors
  driveForward: '#3b82f6',
  driveBackward: '#3b82f6',
  lyPose: '#2dd4bf'
};

export function movementColor(type: string): string {
  return MOVEMENT_COLORS[type] ?? '#888888';
}

export const PID_TERM = { p: '#ef4444', i: '#e8e8e8', d: '#3b82f6' };

export const COLORS = { drive: '#3b82f6', turn: '#ef4444', lyP: '#fb923c', lyLat: '#2dd4bf' };
