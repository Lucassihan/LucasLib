// Shared chassis geometry. Produces the drive-wheel and intake-flexwheel rects
// in the robot's local frame (origin at center, front = up / −Y), so both the
// field Robot and the settings preview draw the exact same hardware.
//
// Everything is in inches; downstream SVGs treat 1 inch = 1 user unit.

import type { DriveWheelCount, IntakePosition } from '$lib/stores/settings.svelte';

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
  intakePosition: IntakePosition
): ChassisParts {
  const { WHEEL_LEN, WHEEL_W, EDGE_BUFFER, FLEX_LEN, FLEX_W, FLEX_COUNT } = CHASSIS;

  // Two wheel columns just inside the side edges. Each holds half the wheels,
  // stretched to the front/back ends (widest wheelbase) and evenly spaced.
  const wheels: Rect[] = [];
  const n = driveWheelCount / 2;
  const colX = width / 2 - EDGE_BUFFER - WHEEL_W / 2;
  const cyTop = -length / 2 + EDGE_BUFFER + WHEEL_LEN / 2;
  const cyBot = length / 2 - EDGE_BUFFER - WHEEL_LEN / 2;
  for (let i = 0; i < n; i++) {
    let cy = cyTop;
    if (n > 1) {
      cy = cyTop + (cyBot - cyTop) * (i / (n - 1));
    }
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
