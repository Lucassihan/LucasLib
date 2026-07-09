// Small-radius element-to-element snapping. Candidates are the key points of
// every element except the ones being dragged. Snap distance is compared in
// SCREEN pixels so it stays small and consistent across zoom levels.

import type { Element, Pt } from '$lib/model/types';
import { holesToInches } from '$lib/geom/chassis';

export const SNAP_PX = 7;

export function collectSnapPoints(elements: Element[], exclude: Set<string>): Pt[] {
  const pts: Pt[] = [];
  for (const el of elements) {
    if (exclude.has(el.id)) continue;
    switch (el.type) {
      case 'robot':
        pts.push({ x: el.x, y: el.y });
        break;
      case 'path':
        for (const p of el.points) pts.push({ x: p.x, y: p.y });
        break;
      case 'shape': {
        pts.push({ x: el.x, y: el.y });
        const hw = el.w / 2;
        const hh = el.h / 2;
        pts.push({ x: el.x + hw, y: el.y }, { x: el.x - hw, y: el.y }, { x: el.x, y: el.y + hh }, { x: el.x, y: el.y - hh });
        break;
      }
      case 'stamp':
        pts.push({ x: el.x, y: el.y });
        break;
      case 'text':
        pts.push({ x: el.x, y: el.y });
        break;
    }
  }
  return pts;
}

/** Snap a field point to the nearest candidate if within SNAP_PX screen px. */
export function snapField(p: Pt, candidates: Pt[], scale: number): Pt {
  let best: Pt | null = null;
  let bestD = SNAP_PX / scale;
  for (const c of candidates) {
    const d = Math.hypot(c.x - p.x, c.y - p.y);
    if (d < bestD) {
      bestD = d;
      best = c;
    }
  }
  return best ?? p;
}
