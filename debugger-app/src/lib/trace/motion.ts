// What a trajectory follow actually did, read back off its ticks.
//
// Averages come off the raw samples — that is what averaging is for. Peaks cannot: chassis.v is a
// one-interval difference of a pose that arrives in bursts, so a single sample misses the true
// speed by ~10 in/s either way, and differencing two of them has produced 3493 in/s² (9 g) of
// acceleration the robot never saw. A peak read off that reports the worst sample, not the worst
// moment. Everything below the averages is therefore measured on a 5-sample centred mean, which
// cuts that noise by about half and costs nothing here — this is a readout, not a control path.

import type { Tick } from './types';

export interface MotionStats {
  avgV: number; // in/s, magnitude
  avgW: number; // deg/s, magnitude
  maxV: number; // in/s
  maxW: number; // deg/s
  maxAccel: number; // in/s²
  n: number;
}

const EMPTY: MotionStats = { avgV: 0, avgW: 0, maxV: 0, maxW: 0, maxAccel: 0, n: 0 };

const WINDOW = 5;

/** Centred moving average, clamped at both ends so the series keeps its length. */
function smoothed(values: number[]): number[] {
  const half = (WINDOW - 1) / 2;
  return values.map((_, i) => {
    let sum = 0;
    for (let k = -half; k <= half; k++) {
      sum += values[Math.min(values.length - 1, Math.max(0, i + k))];
    }
    return sum / WINDOW;
  });
}

export function motionStats(ticks: Tick[]): MotionStats {
  if (ticks.length === 0) return EMPTY;

  const v = smoothed(ticks.map((t) => Math.abs(t.v)));
  const w = smoothed(ticks.map((t) => Math.abs(t.w)));

  let sumV = 0;
  let sumW = 0;
  let maxV = 0;
  let maxW = 0;
  for (let i = 0; i < ticks.length; i++) {
    sumV += Math.abs(ticks[i].v);
    sumW += Math.abs(ticks[i].w);
    maxV = Math.max(maxV, v[i]);
    maxW = Math.max(maxW, w[i]);
  }

  // Ticks carry a millisecond clock, and it is not perfectly uniform, so the interval is measured
  // rather than assumed. Sub-millisecond gaps are skipped instead of dividing by them.
  let maxAccel = 0;
  for (let i = 1; i < ticks.length; i++) {
    const dt = (ticks[i].t - ticks[i - 1].t) / 1000;
    if (dt > 1e-4) maxAccel = Math.max(maxAccel, Math.abs(v[i] - v[i - 1]) / dt);
  }

  return {
    avgV: sumV / ticks.length,
    avgW: sumW / ticks.length,
    maxV,
    maxW,
    maxAccel,
    n: ticks.length
  };
}
