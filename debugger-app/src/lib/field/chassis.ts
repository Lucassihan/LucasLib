// Shared chassis geometry. Produces the drive-wheel and intake-flexwheel rects
// in the robot's local frame (origin at center, front = up / −Y), so both the
// field Robot and the settings preview draw the exact same hardware.
//
// Everything is in inches; downstream SVGs treat 1 inch = 1 user unit.

import type { DriveWheelCount, IntakePosition, SixWheelLayout } from '$lib/stores/settings.svelte';

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

  // Two wheel columns just inside the side edges. Each holds the rows for this
  // wheel count / layout, stretched to the front/back ends (widest wheelbase).
  const wheels: Rect[] = [];
  const colX = width / 2 - EDGE_BUFFER - WHEEL_W / 2;
  for (const cy of wheelRowYs(length, driveWheelCount, layout)) {
    wheels.push({ x: -colX - WHEEL_W / 2, y: cy - WHEEL_LEN / 2, w: WHEEL_W, h: WHEEL_LEN });
    wheels.push({ x: colX - WHEEL_W / 2, y: cy - WHEEL_LEN / 2, w: WHEEL_W, h: WHEEL_LEN });
  }

  // Row of flexwheels straddling the intake edge, each half inset, distributed
  // evenly between the two wheel columns.
  let edgeY = -length / 2;
  if (intakePosition === 'back') {
    edgeY = length / 2;
  }
  const innerLeft = -width / 2 + EDGE_BUFFER + WHEEL_W;
  const innerRight = width / 2 - EDGE_BUFFER - WHEEL_W;
  const firstCx = innerLeft + FLEX_W / 2;
  const lastCx = innerRight - FLEX_W / 2;
  const flex: Rect[] = [];
  for (let i = 0; i < FLEX_COUNT; i++) {
    let cx = firstCx;
    if (FLEX_COUNT > 1) {
      cx = firstCx + (lastCx - firstCx) * (i / (FLEX_COUNT - 1));
    }
    flex.push({ x: cx - FLEX_W / 2, y: edgeY - FLEX_LEN / 2, w: FLEX_W, h: FLEX_LEN });
  }

  return { wheels, flex };
}

// n wheel-row centres evenly spread between the front and back ends.
function rowCenters(length: number, n: number): number[] {
  const { WHEEL_LEN, EDGE_BUFFER } = CHASSIS;
  const cyTop = -length / 2 + EDGE_BUFFER + WHEEL_LEN / 2;
  const cyBot = length / 2 - EDGE_BUFFER - WHEEL_LEN / 2;
  const out: number[] = [];
  for (let i = 0; i < n; i++) {
    out.push(n > 1 ? cyTop + (cyBot - cyTop) * (i / (n - 1)) : 0);
  }
  return out;
}

// Local-frame y of each wheel ROW centre, front (−y) to back (+y).
//
// A 6-wheel drive can use an 8-wheel footprint with one slot left empty:
// "gapFront" drops the 2nd row, "gapBack" drops the 3rd. "centered" spreads
// three rows evenly (the usual layout). Only meaningful for 6 wheels.
export function wheelRowYs(
  length: number,
  driveWheelCount: DriveWheelCount,
  layout: SixWheelLayout = 'centered'
): number[] {
  if (driveWheelCount === 6 && layout !== 'centered') {
    const four = rowCenters(length, 4); // 8-wheel spacing
    if (layout === 'gapFront') {
      return [four[0], four[2], four[3]]; // no wheel in the 2nd slot
    }
    return [four[0], four[1], four[3]]; // gapBack: no wheel in the 3rd slot
  }
  return rowCenters(length, driveWheelCount / 2);
}

// Tracking-center snap positions: each wheel row plus the midpoint between
// adjacent rows, interleaved front→back. Length = 2·rows − 1 (always odd, so
// the middle entry is the geometric centre).
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

/** Clamp a stored tracking-center index to the valid range for a wheel count. */
export function clampTrackingIndex(index: number, driveWheelCount: DriveWheelCount): number {
  const max = driveWheelCount - 2; // 2·(count/2) − 2 = count − 2 snap positions, 0-based
  return Math.max(0, Math.min(index, max));
}
