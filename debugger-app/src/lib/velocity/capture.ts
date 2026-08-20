// capture.ts — reads the file velocityTuner writes and derives everything from it.
//
// The robot logs only step responses. Each trial ran until its window mean
// stopped climbing, so the tail of every rise IS that voltage's steady-state
// point — the sweep is derived here rather than collected twice.

import type { Pt } from './fit';

export interface Trial {
  volts: number;
  samples: Pt[]; // (seconds, speed)
}

export interface Capture {
  drift: number;
  window: number;
  unit: string; // in/s or deg/s — which channel the file came from
  trials: Trial[];
}

export const EMPTY_CAPTURE: Capture = { drift: 0, window: 25, unit: 'in/s', trials: [] };

/**
 * Parse lin.txt / ang.txt. Line-keyed rather than "every number in reading
 * order", because the trial boundaries and the header carry meaning.
 */
export function parseCapture(text: string): Capture {
  const cap: Capture = { ...EMPTY_CAPTURE, trials: [] };
  let cur: Trial | null = null;

  for (const raw of text.split('\n')) {
    const line = raw.trim();
    if (!line) continue;

    const num = (s: string) => parseFloat(s.replace(/[^-\d.]/g, ''));

    if (line.startsWith('drift')) cap.drift = num(line.slice(5)) || 0;
    else if (line.startsWith('window')) cap.window = Math.max(1, Math.round(num(line.slice(6))) || 25);
    else if (line.startsWith('unit')) cap.unit = line.slice(4).trim() || 'in/s';
    else if (line.startsWith('trial')) {
      cur = { volts: num(line.slice(5)) || 0, samples: [] };
      cap.trials.push(cur);
    } else if (cur) {
      const n = line.match(/-?\d*\.?\d+(?:[eE][-+]?\d+)?/g);
      if (n && n.length >= 2) cur.samples.push({ x: Number(n[0]), y: Number(n[1]) });
    }
  }

  return cap;
}

/** The settle window of a trial: the last `window` samples it recorded. */
export function settleTail(trial: Trial, window: number): Pt[] {
  const n = Math.min(window, trial.samples.length);
  return n > 0 ? trial.samples.slice(-n) : [];
}

/** Steady speed of a trial — the average across its settle window. */
export function settledSpeed(trial: Trial, window: number): number {
  const tail = settleTail(trial, window);
  if (!tail.length) return 0;
  return tail.reduce((a, p) => a + p.y, 0) / tail.length;
}

/**
 * How much the window mean was still moving at the end of the trial — the same
 * number the robot's settle test watched. Should sit under `drift`.
 */
export function settleDrift(trial: Trial, window: number): number {
  const s = trial.samples;
  if (s.length < 2 * window) return 0;
  const mean = (from: number) => {
    let sum = 0;
    for (let i = 0; i < window; i++) sum += s[s.length - 1 - from - i].y;
    return sum / window;
  };
  return Math.abs(mean(0) - mean(window));
}

/** One (volts, steady speed) point per trial — the sweep, derived. */
export function sweepFrom(cap: Capture): Pt[] {
  return cap.trials
    .filter((t) => t.samples.length > 0)
    .map((t) => ({ x: t.volts, y: settledSpeed(t, cap.window) }));
}
