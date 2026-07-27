// Shared bezier-spline geometry + 2D-motion-profile bake, in plain TS so both the
// Trajectory Generator store (runes) and the sandbox sim (plain) can use it.
//
// An Anchor carries collinear tangent handles: `ang` is the travel direction
// through the point (deg, field convention 0 = +Y, CW+), `out`/`in` the handle
// lengths on each side. The velocity bake follows the ramsete-site 2DMP: a
// wheel-saturation curvature ceiling, then forward/backward acceleration passes.

export interface Anchor {
  x: number;
  y: number;
  ang: number; // travel direction through the anchor, deg
  out: number; // outgoing handle length, in
  in: number; // incoming handle length, in
}

export interface Pt {
  x: number;
  y: number;
}

/** A dense spline sample that remembers which segment + t it came from. */
export interface DenseSample extends Pt {
  seg: number;
  t: number;
}

/** One baked trajectory point: pose + planned speed + arc position. */
export interface TrajPoint {
  x: number;
  y: number;
  h: number; // tangent heading, deg (field convention)
  v: number; // planned speed, in/s
  s: number; // cumulative arc length, in
}

export interface BakedTrajectory {
  points: TrajPoint[];
  dense: DenseSample[];
  total: number; // path length, in
}

export interface BakeParams {
  maxVel: number; // top speed, in/s
  maxAccel: number; // accel/decel budget, in/s^2
  startVel: number; // speed entering the trajectory, in/s
  exitVel: number; // speed to leave the final point at, in/s
  spacing?: number; // baked-point spacing, in (default 2)
}

/** User-facing follow settings for a [TRAJECTORY] (sliders). */
export interface TrajectoryParams {
  maxVel: number; // in/s
  exitVel: number; // in/s
  maxAccel: number; // in/s^2
  beta: number; // RAMSETE stiffness
  zeta: number; // RAMSETE damping
}

/** A [TRAJECTORY]'s spline + follow settings (stored per marker). */
export interface TrajectoryData {
  anchors: Anchor[];
  params: TrajectoryParams;
}

/** Trajectories keyed by statement index. */
export type TrajectoryMap = Record<number, TrajectoryData>;

export const DEFAULT_TRAJ_PARAMS: TrajectoryParams = { maxVel: 60, exitVel: 0, maxAccel: 60, beta: 2, zeta: 0.7 };

const D2R = Math.PI / 180;
const R2D = 180 / Math.PI;

export const dist = (a: Pt, b: Pt) => Math.hypot(b.x - a.x, b.y - a.y);
export const normDeg = (deg: number) => ((deg % 360) + 360) % 360;
export const round1 = (v: number) => Math.round(v * 10) / 10;

/** Unit vector of a field heading (0 = +Y, CW+). */
export function dir(angDeg: number): Pt {
  return { x: Math.sin(angDeg * D2R), y: Math.cos(angDeg * D2R) };
}

/** The four cubic control points of the segment anchors a -> b. */
export function segControls(a: Anchor, b: Anchor): [Pt, Pt, Pt, Pt] {
  const da = dir(a.ang);
  const db = dir(b.ang);
  return [
    { x: a.x, y: a.y },
    { x: a.x + da.x * a.out, y: a.y + da.y * a.out },
    { x: b.x - db.x * b.in, y: b.y - db.y * b.in },
    { x: b.x, y: b.y }
  ];
}

function bez(c: [Pt, Pt, Pt, Pt], t: number): Pt {
  const u = 1 - t;
  const w0 = u * u * u;
  const w1 = 3 * u * u * t;
  const w2 = 3 * u * t * t;
  const w3 = t * t * t;
  return {
    x: w0 * c[0].x + w1 * c[1].x + w2 * c[2].x + w3 * c[3].x,
    y: w0 * c[0].y + w1 * c[1].y + w2 * c[2].y + w3 * c[3].y
  };
}

/**
 * A shape-preserving de Casteljau split of segment a->b at parameter t. Returns
 * the new handle lengths for the neighbours plus the inserted middle anchor.
 * Used by the store's insert-on-path editing.
 */
export function splitAnchor(a: Anchor, b: Anchor, t: number): { outA: number; inB: number; mid: Anchor } {
  const [q0, q1, q2, q3] = segControls(a, b);
  const lerp = (p: Pt, q: Pt): Pt => ({ x: p.x + (q.x - p.x) * t, y: p.y + (q.y - p.y) * t });
  const l1 = lerp(q0, q1);
  const hMid = lerp(q1, q2);
  const r2 = lerp(q2, q3);
  const l2 = lerp(l1, hMid);
  const r1 = lerp(hMid, r2);
  const m = lerp(l2, r1);
  return {
    outA: round1(dist(q0, l1)),
    inB: round1(dist(r2, q3)),
    mid: {
      x: round1(m.x),
      y: round1(m.y),
      ang: round1(normDeg(Math.atan2(r1.x - l2.x, r1.y - l2.y) * R2D)),
      in: round1(Math.max(3, dist(l2, m))),
      out: round1(Math.max(3, dist(m, r1)))
    }
  };
}

// --- speed → color (three-stop ramp: amber → lime → green) ---------------------
const RAMP: [number, number, number][] = [
  [0xf5, 0x9e, 0x0b],
  [0xb5, 0xce, 0x49],
  [0x3e, 0xc9, 0x7e]
];

export function speedColor(v: number, vmax: number): string {
  const t = Math.max(0, Math.min(1, v / Math.max(1, vmax)));
  const f = t < 0.5 ? t * 2 : (t - 0.5) * 2;
  const [a, b] = t < 0.5 ? [RAMP[0], RAMP[1]] : [RAMP[1], RAMP[2]];
  const c = a.map((x, i) => Math.round(x + (b[i] - x) * f));
  return `rgb(${c[0]}, ${c[1]}, ${c[2]})`;
}

/**
 * Bake a spline into a time-parameterizable trajectory: even arc-length samples
 * with a planned speed at each, per the ramsete-site 2DMP.
 *   ceiling(κ) = maxVel / (1 + |κ|·T/2)   (outer wheel can't exceed maxVel)
 *   forward pass  from startVel, bounded by maxAccel
 *   backward pass to exitVel,   bounded by maxAccel
 * `trackwidth` T sets how hard curvature discounts speed.
 */
export function bakeTrajectory(anchors: Anchor[], params: BakeParams, trackwidth: number): BakedTrajectory {
  if (anchors.length < 2) return { points: [], dense: [], total: 0 };
  const { maxVel, maxAccel, startVel, exitVel } = params;
  const spacing = Math.max(0.5, params.spacing ?? 2);
  const T = Math.max(1, trackwidth);

  // 1. dense-sample the spline
  const dense: DenseSample[] = [];
  for (let i = 0; i < anchors.length - 1; i++) {
    const c = segControls(anchors[i], anchors[i + 1]);
    const polyLen = dist(c[0], c[1]) + dist(c[1], c[2]) + dist(c[2], c[3]);
    const n = Math.min(400, Math.max(16, Math.ceil(polyLen / 0.5)));
    for (let k = i === 0 ? 0 : 1; k <= n; k++) {
      const t = k / n;
      dense.push({ ...bez(c, t), seg: i, t });
    }
  }

  // 2. arc length, then even-spacing resample
  const s = [0];
  for (let i = 1; i < dense.length; i++) s[i] = s[i - 1] + dist(dense[i - 1], dense[i]);
  const total = s[s.length - 1];

  const pts: Pt[] = [];
  let di = 0;
  for (let target = 0; target < total; target += spacing) {
    while (di < dense.length - 1 && s[di + 1] < target) di++;
    const span = s[di + 1] - s[di];
    const u = span > 1e-9 ? (target - s[di]) / span : 0;
    pts.push({ x: dense[di].x + (dense[di + 1].x - dense[di].x) * u, y: dense[di].y + (dense[di + 1].y - dense[di].y) * u });
  }
  pts.push({ ...dense[dense.length - 1] });

  const n = pts.length;

  // 3. Menger curvature at interior points (endpoints straight)
  const kappa = new Array(n).fill(0);
  for (let i = 1; i < n - 1; i++) {
    const p1 = pts[i - 1];
    const p2 = pts[i];
    const p3 = pts[i + 1];
    const cross = (p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x);
    const d12 = dist(p1, p2);
    const d23 = dist(p2, p3);
    const d13 = dist(p1, p3);
    kappa[i] = d12 * d23 * d13 > 1e-9 ? (2 * Math.abs(cross)) / (d12 * d23 * d13) : 0;
  }

  // 4. wheel-saturation ceiling, then forward + backward accel passes
  const ceil = kappa.map((k) => maxVel / (1 + (Math.abs(k) * T) / 2));
  const vel = new Array(n).fill(0);
  vel[0] = Math.min(ceil[0], Math.max(0, startVel));
  for (let i = 1; i < n; i++) {
    const d = dist(pts[i - 1], pts[i]);
    vel[i] = Math.min(ceil[i], Math.sqrt(vel[i - 1] * vel[i - 1] + 2 * maxAccel * d));
  }
  vel[n - 1] = Math.min(vel[n - 1], Math.max(0, exitVel));
  for (let i = n - 2; i >= 0; i--) {
    const d = dist(pts[i], pts[i + 1]);
    vel[i] = Math.min(vel[i], Math.sqrt(vel[i + 1] * vel[i + 1] + 2 * maxAccel * d));
  }

  // 5. per-point tangent heading + arc position
  let arc = 0;
  const points: TrajPoint[] = pts.map((p, i) => {
    if (i > 0) arc += dist(pts[i - 1], p);
    const a = pts[Math.max(0, i - 1)];
    const b = pts[Math.min(n - 1, i + 1)];
    const h = normDeg(Math.atan2(b.x - a.x, b.y - a.y) * R2D);
    return { x: p.x, y: p.y, h, v: vel[i], s: arc };
  });
  return { points, dense, total };
}

/** A sensible default 2-anchor spline leaving `pose` along its heading. */
export function defaultTrajectory(pose: { x: number; y: number; h: number }): Anchor[] {
  const d = dir(pose.h);
  const reach = 36;
  return [
    { x: round1(pose.x), y: round1(pose.y), ang: round1(normDeg(pose.h)), out: 18, in: 18 },
    { x: round1(pose.x + d.x * reach), y: round1(pose.y + d.y * reach), ang: round1(normDeg(pose.h)), out: 18, in: 18 }
  ];
}
