// Velocity Controller state — one pasted capture, the trial picked out of it,
// and the gains it produces. The tab is a measurement worksheet for
// VelocityController::setConstants(); the procedure is in velocityController.h.
//
//   settled tails of every trial → kV, kS
//   the picked trial's rise      → K, L, T → kA, kP, kI
//
// Times are SECONDS — the unit the library's own clock keeps.

import { fitSteady, fitFopdt, gainsFrom, type Pt, type SteadyFit, type FopdtFit, type Gains } from '$lib/velocity/fit';
import { parseCapture, sweepFrom, settledSpeed, settleDrift, EMPTY_CAPTURE, type Capture } from '$lib/velocity/capture';

const STORAGE_KEY = 'lucaslib-debugger.velocity';

interface VelocityState {
  text: string; // the pasted lin.txt / ang.txt
  selected: number; // index of the trial the step fit uses
  lambdaFactor: number; // λ = lambdaFactor · T
}

const defaults: VelocityState = { text: '', selected: 0, lambdaFactor: 0.5 };

function load(): VelocityState {
  if (typeof localStorage === 'undefined') return { ...defaults };
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (raw) {
      const p = JSON.parse(raw) as Partial<VelocityState>;
      return {
        text: typeof p.text === 'string' ? p.text : '',
        selected: typeof p.selected === 'number' && p.selected >= 0 ? p.selected : 0,
        lambdaFactor: typeof p.lambdaFactor === 'number' && p.lambdaFactor > 0 ? p.lambdaFactor : 0.5
      };
    }
  } catch {
    // corrupt — reset
  }
  return { ...defaults };
}

export const velocity = $state<VelocityState>(load());

$effect.root(() => {
  $effect(() => {
    if (typeof localStorage === 'undefined') return;
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(velocity));
    } catch {
      // quota — ignore
    }
  });
});

export interface TrialFit {
  volts: number;
  speed: number; // settled average
  drift: number; // how much the window mean was still moving, against the file's drift
  fopdt: FopdtFit;
}

export interface VelocityFit {
  capture: Capture;
  sweep: Pt[];
  steady: SteadyFit;
  trials: TrialFit[];
  picked: TrialFit | null;
  gains: Gains;
  unit: string;
  instance: string;
}

// memoized — every trial gets its own iterative FOPDT solve, and the textarea
// re-renders on each keystroke
let cacheKey = '';
let cacheVal: VelocityFit | null = null;

export function getFit(): VelocityFit {
  const key = velocity.text + '|' + velocity.selected + '|' + velocity.lambdaFactor;
  if (key === cacheKey && cacheVal) return cacheVal;

  const capture = velocity.text.trim() ? parseCapture(velocity.text) : { ...EMPTY_CAPTURE, trials: [] };
  const sweep = sweepFrom(capture);
  const steady = fitSteady(sweep);

  // Every trial is fitted, not just the picked one: T and L describe the drive
  // and should come out the same each run, so the spread across trials is the
  // honest read on whether a run went wrong.
  const trials: TrialFit[] = capture.trials.map((t) => ({
    volts: t.volts,
    speed: settledSpeed(t, capture.window),
    drift: settleDrift(t, capture.window),
    fopdt: fitFopdt(t.samples, t.volts)
  }));

  const picked = trials[Math.min(velocity.selected, trials.length - 1)] ?? null;

  const unit = capture.unit || 'in/s';
  cacheVal = {
    capture,
    sweep,
    steady,
    trials,
    picked,
    gains: gainsFrom(steady, picked ? picked.fopdt : fitFopdt([], 0), velocity.lambdaFactor),
    unit,
    instance: unit.startsWith('deg') ? 'omegaController' : 'velocityController'
  };
  cacheKey = key;
  return cacheVal;
}

export function pickTrial(i: number) {
  velocity.selected = i;
}

/** Enough digits that a small gain (kA runs ~1e-3) survives the round trip. */
export function fmtGain(v: number): string {
  if (!isFinite(v)) return '0';
  const a = Math.abs(v);
  if (a === 0) return '0';
  if (a >= 100) return v.toFixed(1);
  if (a >= 1) return v.toFixed(3);
  if (a >= 0.01) return v.toFixed(4);
  return v.toPrecision(3);
}

export const SET_CONSTANTS_PARAMS = ['kV', 'kA', 'kS', 'kP', 'kI'];

export function exportCpp(): string {
  const fit = getFit();
  const g = fit.gains;
  return `${fit.instance}.setConstants(${fmtGain(g.kV)}, ${fmtGain(g.kA)}, ${fmtGain(g.kS)}, ${fmtGain(g.kP)}, ${fmtGain(g.kI)});`;
}
