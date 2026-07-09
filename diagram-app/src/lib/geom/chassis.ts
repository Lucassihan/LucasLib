// Shared chassis geometry, ported from the LucasLib debugger. Produces the
// drive-wheel and intake-flexwheel rects in the robot's local frame (origin at
// center, front = up / −Y). Everything is in inches; the canvas treats 1 inch
// as 1 world unit.

export type DriveWheelCount = 4 | 6 | 8;
export type IntakePosition = 'front' | 'back';
export type SixWheelLayout = 'centered' | 'gapFront' | 'gapBack';

export const HOLE_INCHES = 0.5;
export function holesToInches(holes: number): number {
  return holes * HOLE_INCHES;
}

export const CHASSIS = {
  WHEEL_LEN: 3.25, // VEX wheel diameter, along the length axis
  WHEEL_W: 1.2, // wheel thickness, along the width axis
  EDGE_BUFFER: 0.4, // tiny clearance from the chassis edge
  FLEX_LEN: 2, // flexwheel length (straddles the edge, half inset)
  FLEX_W: 1, // flexwheel width
  FLEX_COUNT: 4
};

export interface Rect {
  x: number;
  y: number;
  w: number;
  h: number;
}

export interface ChassisParts {
  wheels: Rect[];
  flex: Rect[];
}

export function chassisParts(
  width: number,
  length: number,
  driveWheelCount: DriveWheelCount,
  intakePosition: IntakePosition,
  layout: SixWheelLayout = 'centered'
): ChassisParts {
  const { WHEEL_LEN, WHEEL_W, EDGE_BUFFER, FLEX_LEN, FLEX_W, FLEX_COUNT } = CHASSIS;

  const wheels: Rect[] = [];
  const colX = width / 2 - EDGE_BUFFER - WHEEL_W / 2;
  for (const cy of wheelRowYs(length, driveWheelCount, layout)) {
    wheels.push({ x: -colX - WHEEL_W / 2, y: cy - WHEEL_LEN / 2, w: WHEEL_W, h: WHEEL_LEN });
    wheels.push({ x: colX - WHEEL_W / 2, y: cy - WHEEL_LEN / 2, w: WHEEL_W, h: WHEEL_LEN });
  }

  let edgeY = -length / 2;
  if (intakePosition === 'back') edgeY = length / 2;
  const innerLeft = -width / 2 + EDGE_BUFFER + WHEEL_W;
  const innerRight = width / 2 - EDGE_BUFFER - WHEEL_W;
  const firstCx = innerLeft + FLEX_W / 2;
  const lastCx = innerRight - FLEX_W / 2;
  const flex: Rect[] = [];
  for (let i = 0; i < FLEX_COUNT; i++) {
    let cx = firstCx;
    if (FLEX_COUNT > 1) cx = firstCx + (lastCx - firstCx) * (i / (FLEX_COUNT - 1));
    flex.push({ x: cx - FLEX_W / 2, y: edgeY - FLEX_LEN / 2, w: FLEX_W, h: FLEX_LEN });
  }

  return { wheels, flex };
}

function rowCenters(length: number, n: number): number[] {
  const { WHEEL_LEN, EDGE_BUFFER } = CHASSIS;
  const cyTop = -length / 2 + EDGE_BUFFER + WHEEL_LEN / 2;
  const cyBot = length / 2 - EDGE_BUFFER - WHEEL_LEN / 2;
  const out: number[] = [];
  for (let i = 0; i < n; i++) out.push(n > 1 ? cyTop + (cyBot - cyTop) * (i / (n - 1)) : 0);
  return out;
}

export function wheelRowYs(
  length: number,
  driveWheelCount: DriveWheelCount,
  layout: SixWheelLayout = 'centered'
): number[] {
  if (driveWheelCount === 6 && layout !== 'centered') {
    const four = rowCenters(length, 4);
    if (layout === 'gapFront') return [four[0], four[2], four[3]];
    return [four[0], four[1], four[3]];
  }
  return rowCenters(length, driveWheelCount / 2);
}

// Tracking-center snap positions: each wheel row plus the midpoint between
// adjacent rows, interleaved front→back. Length = 2·rows − 1 (always odd).
export function trackingSnapYs(
  length: number,
  driveWheelCount: DriveWheelCount,
  layout: SixWheelLayout = 'centered'
): number[] {
  const rows = wheelRowYs(length, driveWheelCount, layout);
  const snaps: number[] = [];
  for (let i = 0; i < rows.length; i++) {
    snaps.push(rows[i]);
    if (i < rows.length - 1) snaps.push((rows[i] + rows[i + 1]) / 2);
  }
  return snaps;
}

export function clampTrackingIndex(index: number, driveWheelCount: DriveWheelCount): number {
  const max = driveWheelCount - 2;
  return Math.max(0, Math.min(index, max));
}
