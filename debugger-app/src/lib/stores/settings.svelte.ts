// Shared app settings — Svelte 5 reactive module state.
//
// Robot size is entered in HOLES (VEX convention: chassis are counted as
// "X holes long × Y holes wide"). 1 hole = 0.5 inch of structure spacing.

export const HOLE_INCHES = 0.5;

export function holesToInches(holes: number): number {
  return holes * HOLE_INCHES;
}

export type DriveWheelCount = 4 | 6 | 8;
export type IntakePosition = 'front' | 'back';
export type GovernScope = 'total' | 'drive' | 'turn';
export type { FieldBackdrop } from '$lib/field/backdrops';
import { normalizeBackdropId, type FieldBackdrop } from '$lib/field/backdrops';
// 6-wheel row layout: evenly centred, or an 8-wheel footprint with the 2nd
// (gapFront) or 3rd (gapBack) slot left empty.
export type SixWheelLayout = 'centered' | 'gapFront' | 'gapBack';

export type AppMode = 'replay' | 'sandbox' | 'trajectory';

interface Settings {
  // which workspace the app shows: trace replay or the route-writing sandbox
  appMode: AppMode;
  robotWidthHoles: number;
  robotLengthHoles: number;
  driveWheelCount: DriveWheelCount;
  sixWheelLayout: SixWheelLayout;
  intakePosition: IntakePosition;
  showBreakdown: boolean;
  // which output governor the pipeline + inspector focus on
  governScope: GovernScope;
  // tracking center (= center of rotation): index into the wheel-row/midpoint
  // snap positions, front→back. Clamped to the current wheel count on use.
  trackingCenterIndex: number;
  // top-down field render shown behind the field map
  fieldBackdrop: FieldBackdrop;
}

const STORAGE_KEY = 'lucaslib-debugger.settings';

// 30 holes = 15 in — a typical VEX drive footprint.
const defaults: Settings = {
  appMode: 'replay',
  robotWidthHoles: 30,
  robotLengthHoles: 30,
  driveWheelCount: 6,
  sixWheelLayout: 'centered',
  intakePosition: 'front',
  showBreakdown: false,
  governScope: 'total',
  trackingCenterIndex: 2, // geometric centre for the default 6-wheel drive
  fieldBackdrop: 'perimeter'
};

function load(): Settings {
  if (typeof localStorage === 'undefined') {
    return { ...defaults };
  }
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (raw) {
      const merged = { ...defaults, ...JSON.parse(raw) };
      // migrate pre-rename backdrop ids (e.g. 'match-h' → 'override-match-h')
      merged.fieldBackdrop = normalizeBackdropId(merged.fieldBackdrop);
      // 'pursuit' mode was renamed to 'trajectory' (Trajectory Generator)
      if ((merged.appMode as string) === 'pursuit') merged.appMode = 'trajectory';
      return merged;
    }
  } catch {
    // corrupt storage — fall back to defaults
  }
  return { ...defaults };
}

export const settings = $state<Settings>(load());

// Persist on any change.
$effect.root(() => {
  $effect(() => {
    if (typeof localStorage !== 'undefined') {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(settings));
    }
  });
});
