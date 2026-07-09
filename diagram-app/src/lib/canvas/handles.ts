// Draggable handles for the selected element, all in FIELD coords. The canvas
// negates y for display. Rotation math matches the SVG render: a group is drawn
// as translate(x,-y) rotate(A), so a local (lx,ly) [y-down] lands at field
// (x + lx·cosA − ly·sinA, y − lx·sinA − ly·cosA).

import type { Element } from '$lib/model/types';
import { holesToInches } from '$lib/geom/chassis';

export interface Handle {
  id: string;
  x: number;
  y: number;
  kind: 'point' | 'size' | 'rotate';
}

const D2R = Math.PI / 180;
const R2D = 180 / Math.PI;

function localToField(x: number, y: number, A: number, lx: number, ly: number) {
  const a = A * D2R;
  return { x: x + lx * Math.cos(a) - ly * Math.sin(a), y: y - lx * Math.sin(a) - ly * Math.cos(a) };
}
function fieldToLocal(x: number, y: number, A: number, px: number, py: number) {
  const a = A * D2R;
  const ox = px - x;
  const oy = y - py;
  return { lx: ox * Math.cos(a) + oy * Math.sin(a), ly: -ox * Math.sin(a) + oy * Math.cos(a) };
}

export function getHandles(el: Element): Handle[] {
  switch (el.type) {
    case 'robot': {
      const len = holesToInches(el.chassis.lengthHoles);
      const d = len / 2 + 3.5;
      return [
        { id: 'rot', kind: 'rotate', x: el.x + Math.sin(el.heading * D2R) * d, y: el.y + Math.cos(el.heading * D2R) * d }
      ];
    }
    case 'path': {
      const hs: Handle[] = el.points.map((p, i) => ({ id: `p${i}`, kind: 'point' as const, x: p.x, y: p.y }));
      if (el.kind === 'boomerang' && el.boom) {
        const s = el.points[0];
        const t = el.points[1];
        hs.push({ id: 'hStart', kind: 'rotate', x: s.x + Math.sin(el.boom.startHeading * D2R) * 8, y: s.y + Math.cos(el.boom.startHeading * D2R) * 8 });
        hs.push({ id: 'hTarget', kind: 'rotate', x: t.x + Math.sin(el.boom.targetHeading * D2R) * 8, y: t.y + Math.cos(el.boom.targetHeading * D2R) * 8 });
      }
      if (el.kind === 'splineHeading' && el.headingStart != null) {
        const s = el.points[0];
        hs.push({ id: 'hStart', kind: 'rotate', x: s.x + Math.sin(el.headingStart * D2R) * 8, y: s.y + Math.cos(el.headingStart * D2R) * 8 });
        if (el.headingEnd != null) {
          const e = el.points[el.points.length - 1];
          hs.push({ id: 'hEnd', kind: 'rotate', x: e.x + Math.sin(el.headingEnd * D2R) * 8, y: e.y + Math.cos(el.headingEnd * D2R) * 8 });
        }
      }
      return hs;
    }
    case 'shape': {
      if (el.shape === 'circle') {
        return [{ id: 'r', kind: 'size', x: el.x + el.w / 2, y: el.y }];
      }
      const hw = el.w / 2;
      const hh = el.h / 2;
      return [
        { id: 'ne', kind: 'size', x: el.x + hw, y: el.y + hh },
        { id: 'nw', kind: 'size', x: el.x - hw, y: el.y + hh },
        { id: 'se', kind: 'size', x: el.x + hw, y: el.y - hh },
        { id: 'sw', kind: 'size', x: el.x - hw, y: el.y - hh }
      ];
    }
    case 'stamp': {
      const A = el.rotation;
      const hw = el.w / 2;
      const hh = el.h / 2;
      const corners: Array<[string, number, number]> = [
        ['ne', hw, -hh],
        ['nw', -hw, -hh],
        ['se', hw, hh],
        ['sw', -hw, hh]
      ];
      const hs: Handle[] = corners.map(([id, lx, ly]) => {
        const f = localToField(el.x, el.y, A, lx, ly);
        return { id, kind: 'size' as const, x: f.x, y: f.y };
      });
      const rp = localToField(el.x, el.y, A, 0, -(hh + 5));
      hs.push({ id: 'rot', kind: 'rotate', x: rp.x, y: rp.y });
      return hs;
    }
    case 'text': {
      const A = el.rotation ?? 0;
      const rp = localToField(el.x, el.y, A, 0, -(el.size * 1.6 + 4));
      return [{ id: 'rot', kind: 'rotate', x: rp.x, y: rp.y }];
    }
    default:
      return [];
  }
}

export function applyHandle(el: Element, id: string, px: number, py: number): void {
  switch (el.type) {
    case 'robot':
      if (id === 'rot') el.heading = Math.atan2(px - el.x, py - el.y) * R2D;
      return;
    case 'path': {
      if (id.startsWith('p')) {
        const i = parseInt(id.slice(1), 10);
        if (el.points[i]) el.points[i] = { x: px, y: py };
        return;
      }
      if (id === 'hStart') {
        const s = el.points[0];
        const h = Math.atan2(px - s.x, py - s.y) * R2D;
        if (el.kind === 'boomerang' && el.boom) el.boom.startHeading = h;
        else if (el.kind === 'splineHeading') el.headingStart = h;
      } else if (id === 'hTarget' && el.kind === 'boomerang' && el.boom) {
        const t = el.points[1];
        el.boom.targetHeading = Math.atan2(px - t.x, py - t.y) * R2D;
      } else if (id === 'hEnd' && el.kind === 'splineHeading') {
        const e = el.points[el.points.length - 1];
        el.headingEnd = Math.atan2(px - e.x, py - e.y) * R2D;
      }
      return;
    }
    case 'shape': {
      if (el.shape === 'circle') {
        el.w = el.h = Math.max(1, Math.hypot(px - el.x, py - el.y) * 2);
      } else {
        el.w = Math.max(1, Math.abs(px - el.x) * 2);
        el.h = Math.max(1, Math.abs(py - el.y) * 2);
      }
      return;
    }
    case 'stamp': {
      if (id === 'rot') {
        el.rotation = Math.atan2(px - el.x, py - el.y) * R2D;
        return;
      }
      const { lx, ly } = fieldToLocal(el.x, el.y, el.rotation, px, py);
      const aspect = el.h / el.w || 1;
      const half = Math.max(Math.abs(lx), Math.abs(ly) / aspect);
      el.w = Math.max(2, half * 2);
      el.h = el.w * aspect;
      return;
    }
    case 'text':
      if (id === 'rot') el.rotation = Math.atan2(px - el.x, py - el.y) * R2D;
      return;
  }
}
