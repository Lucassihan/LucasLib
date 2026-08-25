// Human-readable movement names + type categories.
// (Design spec: show "Move To", not "moveTo".)

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
  voldrive: 'Voltage Drive',
  'ramsete.trajectory': 'Trajectory',
  'ramsete.boomerangTo': 'Ramsete Boomerang',
  'ramsete.moveTo': 'Ramsete Move To',
  lyPose: 'lyPose',
  // retired — renamed to ramsete.boomerangTo(). Kept so traces recorded before the
  // rename still render with a readable name instead of the raw type.
  'ramsete.quadBezier': 'Quad Bezier',
  // retired — renamed twice: executeTrajectory() → trajectory() → ramsete.trajectory().
  // Kept so traces recorded before each rename still render with a readable name
  // instead of the raw type.
  trajectory: 'Trajectory',
  executeTrajectory: 'Trajectory',
  // retired — the lib went back to driveForward/driveBackward, so `drive` no
  // longer parses in the sandbox. Kept only so traces recorded during the brief
  // single-drive() era still render with a readable name instead of the raw type.
  drive: 'Drive'
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
const ENDS_AT_TARGET = new Set(['moveTo', 'boomerangTo', 'ramsete.moveTo']);

export function endsAtTarget(type: string): boolean {
  return ENDS_AT_TARGET.has(type);
}

/** voldrive — open-loop voltage command (no PID, no exit, no governing). */
export function isVolDrive(type: string): boolean {
  return type === 'voldrive';
}

/** The scheduled follow, which carries a reference pose. boomerangTo bakes its one
 *  curve and then runs the very same loop, so everything keyed off this applies.
 *  quadBezier is its retired name, still in archived traces. */
export function isTrajectory(type: string): boolean {
  return type === 'ramsete.trajectory'
      || type === 'ramsete.boomerangTo'
      || type === 'ramsete.quadBezier';
}

/** ramsete.moveTo — the lyapunov point movement, steered by kLat·v·e_y·sinc. */
export function isRamseteMove(type: string): boolean {
  return type === 'ramsete.moveTo';
}

const MOVEMENT_COLORS: Record<string, string> = {
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
  voldrive: '#a855f7',
  trajectory: '#06b6d4',
  'ramsete.trajectory': '#06b6d4',
  'ramsete.boomerangTo': '#22d3ee',
  'ramsete.moveTo': '#4ade80',
  lyPose: '#2dd4bf',
  // retired — pre-rename traces keep the same cyan
  'ramsete.quadBezier': '#22d3ee',
  executeTrajectory: '#06b6d4',
  // retired — archived single-drive()-era traces keep the same blue
  drive: '#3b82f6'
};

export function movementColor(type: string): string {
  return MOVEMENT_COLORS[type] ?? '#888888';
}

export const PID_TERM = { p: '#ef4444', i: '#e8e8e8', d: '#3b82f6' };

// rsProp / rsLat split ramsete.moveTo's steering into its two halves: headingPID's
// own output, and the lyapunov cross-track coupling stacked on top of it.
export const COLORS = { drive: '#3b82f6', turn: '#ef4444', rsProp: '#fb923c', rsLat: '#4ade80' };
