// speed → color, shared by the Trajectory Generator's planned path and the
// Replay's driven path so a route looks the same in both workspaces.
//
// Traffic-light ramp: red (slow) → yellow (mid) → green (fast), spread across
// the path's OWN speed span (see speedRange) rather than an absolute scale — a
// route that never slows down would otherwise be one flat green line.
//
// Pure module, no store state: the replay half of the app shouldn't have to pull
// in the generator's document model just to draw a colored line.

const RAMP: [number, number, number][] = [
  [0xef, 0x44, 0x44],
  [0xfa, 0xcc, 0x15],
  [0x3e, 0xc9, 0x7e]
];

/** Colour for speed `v` mapped onto the [lo, hi] window (red → yellow → green). */
export function speedColor(v: number, lo: number, hi: number): string {
  const span = Math.max(1e-6, hi - lo);
  const t = Math.max(0, Math.min(1, (v - lo) / span));
  const f = t < 0.5 ? t * 2 : (t - 0.5) * 2;
  const [a, b] = t < 0.5 ? [RAMP[0], RAMP[1]] : [RAMP[1], RAMP[2]];
  const c = a.map((x, i) => Math.round(x + (b[i] - x) * f));
  return `rgb(${c[0]}, ${c[1]}, ${c[2]})`;
}

/**
 * The colour window for a run: its [min, max] speed, so the slowest point reads
 * red and the fastest green. Falls back to absolute [0, maxVel] for a
 * near-constant-speed path (nothing to spread across).
 *
 * Measured data gets the window from the 1st and 99th percentile rather than the
 * true extremes. A single bad sample — an odometry spike divides a real
 * displacement by an almost-zero interval and reports millions of in/s — would
 * otherwise set `hi` so high that every real speed maps to the bottom of the
 * ramp and the whole path renders flat red. Samples outside the window clamp to
 * the ends, which is what they should look like anyway.
 */
export function speedRange(points: { v: number }[], maxVel: number): { lo: number; hi: number } {
  const sorted = points.map((p) => p.v).filter(Number.isFinite).sort((a, b) => a - b);
  if (!sorted.length) return { lo: 0, hi: Math.max(1, maxVel) };

  const lo = sorted[Math.floor((sorted.length - 1) * 0.01)];
  const hi = sorted[Math.ceil((sorted.length - 1) * 0.99)];
  if (hi - lo < 3) return { lo: 0, hi: Math.max(1, maxVel) };
  return { lo, hi };
}
