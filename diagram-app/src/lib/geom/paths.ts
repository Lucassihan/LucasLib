// Path geometry: turns a PathEl's control points into a sampled polyline (field
// coords) plus per-point tangent headings, arc-length table, and — for the
// boomerang kind — the full carrot-pursuit result.

import type { PathEl, Pt } from '$lib/model/types';
import { simulateBoomerang, type BoomResult } from './boomerang';

const D2R = Math.PI / 180;
const R2D = 180 / Math.PI;

export interface SampledPath {
  pts: Pt[];
  headings: number[]; // facing (deg) of the tangent at each pt
  cumLen: number[];
  totalLen: number;
  boom?: BoomResult;
}

function unit(headingDeg: number): Pt {
  return { x: Math.sin(headingDeg * D2R), y: Math.cos(headingDeg * D2R) };
}

/** Circular arc through three points a → through → b, sampled into n points. */
function sampleArc(a: Pt, through: Pt, b: Pt, n: number): Pt[] {
  const ax = a.x, ay = a.y, bx = through.x, by = through.y, cx = b.x, cy = b.y;
  const d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
  if (Math.abs(d) < 1e-9) return [a, b]; // colinear → straight
  const ux =
    ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) + (cx * cx + cy * cy) * (ay - by)) / d;
  const uy =
    ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) + (cx * cx + cy * cy) * (bx - ax)) / d;
  const center = { x: ux, y: uy };
  const r = Math.hypot(ax - ux, ay - uy);
  let a0 = Math.atan2(ay - uy, ax - ux);
  let a1 = Math.atan2(by - uy, bx - ux);
  let a2 = Math.atan2(cy - uy, cx - ux);
  // ensure the sweep passes through `through`
  const norm = (x: number) => {
    while (x < 0) x += Math.PI * 2;
    while (x >= Math.PI * 2) x -= Math.PI * 2;
    return x;
  };
  const rel1 = norm(a1 - a0);
  let end = norm(a2 - a0);
  const ccw = rel1 <= end; // is `through` on the CCW side before `b`?
  if (!ccw) end = end - Math.PI * 2;
  const out: Pt[] = [];
  for (let i = 0; i <= n; i++) {
    const t = i / n;
    const ang = a0 + end * t;
    out.push({ x: center.x + r * Math.cos(ang), y: center.y + r * Math.sin(ang) });
  }
  return out;
}

/** Hermite/Catmull-Rom through control points, with optional fixed end tangents. */
function sampleSpline(points: Pt[], hStart?: number, hEnd?: number | null, perSeg = 20): Pt[] {
  const n = points.length;
  if (n < 2) return points.slice();
  if (n === 2 && hStart == null && hEnd == null) return [points[0], points[1]];
  const out: Pt[] = [];
  for (let i = 0; i < n - 1; i++) {
    const p0 = points[i === 0 ? 0 : i - 1];
    const p1 = points[i];
    const p2 = points[i + 1];
    const p3 = points[i + 2 >= n ? n - 1 : i + 2];
    let m1 = { x: (p2.x - p0.x) * 0.5, y: (p2.y - p0.y) * 0.5 };
    let m2 = { x: (p3.x - p1.x) * 0.5, y: (p3.y - p1.y) * 0.5 };
    const segLen = Math.hypot(p2.x - p1.x, p2.y - p1.y);
    if (i === 0 && hStart != null) {
      const u = unit(hStart);
      m1 = { x: u.x * segLen, y: u.y * segLen };
    }
    if (i === n - 2 && hEnd != null && hEnd !== undefined) {
      const u = unit(hEnd);
      m2 = { x: u.x * segLen, y: u.y * segLen };
    }
    for (let k = 0; k < perSeg; k++) {
      const t = k / perSeg;
      const t2 = t * t;
      const t3 = t2 * t;
      const h00 = 2 * t3 - 3 * t2 + 1;
      const h10 = t3 - 2 * t2 + t;
      const h01 = -2 * t3 + 3 * t2;
      const h11 = t3 - t2;
      out.push({
        x: h00 * p1.x + h10 * m1.x + h01 * p2.x + h11 * m2.x,
        y: h00 * p1.y + h10 * m1.y + h01 * p2.y + h11 * m2.y
      });
    }
  }
  out.push(points[n - 1]);
  return out;
}

function buildTable(pts: Pt[]): { headings: number[]; cumLen: number[]; totalLen: number } {
  const cumLen: number[] = [0];
  let total = 0;
  for (let i = 1; i < pts.length; i++) {
    total += Math.hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    cumLen.push(total);
  }
  const headings: number[] = [];
  for (let i = 0; i < pts.length; i++) {
    const a = pts[Math.max(0, i - 1)];
    const b = pts[Math.min(pts.length - 1, i + 1)];
    headings.push(Math.atan2(b.x - a.x, b.y - a.y) * R2D);
  }
  return { headings, cumLen, totalLen: total };
}

export function samplePath(el: PathEl): SampledPath {
  let pts: Pt[];
  let boom: BoomResult | undefined;

  if (el.kind === 'boomerang' && el.boom && el.points.length >= 2) {
    const start = { ...el.points[0], heading: el.boom.startHeading };
    const target = { ...el.points[1], heading: el.boom.targetHeading };
    boom = simulateBoomerang(start, target, {
      lead: el.boom.lead,
      gLead: el.boom.gLead,
      backward: el.boom.backward
    });
    pts = boom.samples.map((s) => ({ x: s.x, y: s.y }));
    const headings = boom.samples.map((s) => s.heading);
    return { pts, headings, cumLen: boom.cumLen, totalLen: boom.totalLen, boom };
  } else if (el.kind === 'straight') {
    pts = el.points.length >= 2 ? [el.points[0], el.points[el.points.length - 1]] : el.points.slice();
  } else if (el.kind === 'arc' && el.points.length >= 3) {
    pts = sampleArc(el.points[0], el.points[1], el.points[2], 48);
  } else if (el.kind === 'splineHeading') {
    pts = sampleSpline(el.points, el.headingStart, el.headingEnd ?? null);
  } else if (el.kind === 'spline') {
    pts = sampleSpline(el.points);
  } else {
    pts = el.points.slice();
  }

  const table = buildTable(pts);
  return { pts, ...table };
}

/** Pose (x, y, facing) at arc-length fraction t of a sampled path. */
export function poseAtT(sp: SampledPath, t: number): { x: number; y: number; heading: number } {
  const { pts, cumLen, totalLen, headings } = sp;
  if (pts.length === 0) return { x: 0, y: 0, heading: 0 };
  if (pts.length === 1) return { x: pts[0].x, y: pts[0].y, heading: headings[0] ?? 0 };
  const target = t * totalLen;
  let i = 1;
  while (i < pts.length && cumLen[i] < target) i++;
  i = Math.min(i, pts.length - 1);
  const seg = cumLen[i] - cumLen[i - 1] || 1;
  const f = Math.min(1, Math.max(0, (target - cumLen[i - 1]) / seg));
  const wrap = (a: number) => {
    while (a > 180) a -= 360;
    while (a < -180) a += 360;
    return a;
  };
  const ha = headings[i - 1];
  const hb = headings[i - 1] + wrap(headings[i] - headings[i - 1]);
  return {
    x: pts[i - 1].x + (pts[i].x - pts[i - 1].x) * f,
    y: pts[i - 1].y + (pts[i].y - pts[i - 1].y) * f,
    heading: ha + (hb - ha) * f
  };
}
