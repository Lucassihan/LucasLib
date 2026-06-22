// Field coordinate system.
//
// Convention confirmed from LucasLib odometry.cpp:
//   - heading 0 = +Y (north / up), clockwise positive
//   - forward vector = (sin h, cos h)  =>  +X is east/right, +Y is north/up
//   - point heading = atan2(targetX - x, targetY - y)
//
// Origin + field size are configurable so we can reconcile with the real
// start_pose values later (and the readWall ±48 question — Phase 7) without
// touching any rendering code.

export interface FieldConfig {
  /** Full field width/height in inches (VEX field = 144"). */
  sizeInches: number;
  /** Where (0,0) sits in field coordinates. */
  origin: 'center' | 'corner';
}

export const defaultField: FieldConfig = {
  sizeInches: 144,
  origin: 'center'
};

export interface ScreenPoint {
  sx: number;
  sy: number;
}

/**
 * Map a field-space point (inches) into SVG viewBox units.
 *
 * The field SVG uses a viewBox of [0, 0, size, size]. Field +Y is up, but SVG
 * y grows downward, so we flip Y here. The result is in the same units as the
 * viewBox, so strokes etc. should use vector-effect="non-scaling-stroke".
 */
export function toScreen(x: number, y: number, cfg: FieldConfig = defaultField): ScreenPoint {
  const s = cfg.sizeInches;

  let fx = x;
  let fy = y;
  if (cfg.origin === 'center') {
    fx = x + s / 2;
    fy = y + s / 2;
  }

  return { sx: fx, sy: s - fy };
}

/** Scalar inches → viewBox units (identity today; kept for clarity at call sites). */
export function scaleInches(inches: number, _cfg: FieldConfig = defaultField): number {
  return inches;
}
