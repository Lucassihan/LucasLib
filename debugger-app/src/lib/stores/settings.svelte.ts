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

interface Settings {
  robotWidthHoles: number;
  robotLengthHoles: number;
  driveWheelCount: DriveWheelCount;
  intakePosition: IntakePosition;
  showBreakdown: boolean;
  // which output governor the pipeline + inspector focus on
  governScope: GovernScope;
}

const STORAGE_KEY = 'lucaslib-debugger.settings';

// 30 holes = 15 in — a typical VEX drive footprint.
const defaults: Settings = {
  robotWidthHoles: 30,
  robotLengthHoles: 30,
  driveWheelCount: 6,
  intakePosition: 'front',
  showBreakdown: false,
  governScope: 'total'
};

function load(): Settings {
  if (typeof localStorage === 'undefined') {
    return { ...defaults };
  }
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (raw) {
      return { ...defaults, ...JSON.parse(raw) };
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
