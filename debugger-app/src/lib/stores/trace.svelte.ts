// Loaded trace + playback state (Svelte 5 reactive module state).
// On boot, loads the auton currently marked active in the library (which is
// seeded with sample autons on first run). loadTrace() records new imports
// into the library too.

import { parseTrace, stripMarkers } from '$lib/trace/parse';
import type { Trace, Tick, Movement } from '$lib/trace/types';
import { library, addAuton, setActive, getAuton } from '$lib/stores/library.svelte';

function initialText(): string {
  const activeId = library.activeId;
  if (activeId) {
    const a = getAuton(activeId);
    if (a) return a.text;
  }
  return library.items[0]?.text ?? '';
}

export const traceState = $state<{
  trace: Trace;
  time: number; // current scrub position, ms
  playing: boolean;
}>({
  trace: parseTrace(initialText()),
  time: 0,
  playing: false
});

/** Load a freshly-imported trace and add it to the library. */
export function loadTrace(text: string, fallbackName = 'imported') {
  // Strip on-robot console markers up front so the library stores clean text
  // (name extraction, stats, and re-parse all work on the bare trace).
  const entry = addAuton(stripMarkers(text), fallbackName);
  traceState.trace = parseTrace(entry.text);
  traceState.time = 0;
  traceState.playing = false;
}

/** Load an already-saved library entry by id (no new entry created). */
export function loadFromLibrary(id: string) {
  const a = getAuton(id);
  if (!a) return;
  setActive(id);
  traceState.trace = parseTrace(a.text);
  traceState.time = 0;
  traceState.playing = false;
}

const wrapDeg = (a: number) => {
  while (a > 180) a -= 360;
  while (a < -180) a += 360;
  return a;
};

const lerp = (a: number, b: number, u: number) => a + (b - a) * u;

/** Index of the last tick at or before `time` (binary search). */
function indexAt(time: number): number {
  const ticks = traceState.trace.ticks;
  if (ticks.length === 0) return -1;
  if (time <= ticks[0].t) return 0;
  if (time >= ticks[ticks.length - 1].t) return ticks.length - 1;
  let lo = 0;
  let hi = ticks.length - 1;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (ticks[mid].t <= time) lo = mid;
    else hi = mid;
  }
  return lo;
}

/** Interpolated robot pose at `time`. */
export function poseAt(time: number): { x: number; y: number; h: number } {
  const ticks = traceState.trace.ticks;
  if (ticks.length === 0) return { x: 0, y: 0, h: 0 };
  const i = indexAt(time);
  const a = ticks[i];
  const b = ticks[Math.min(i + 1, ticks.length - 1)];
  const span = b.t - a.t;
  const u = span > 0 ? Math.max(0, Math.min(1, (time - a.t) / span)) : 0;
  return {
    x: lerp(a.x, b.x, u),
    y: lerp(a.y, b.y, u),
    h: a.h + wrapDeg(b.h - a.h) * u
  };
}

/** The tick active at `time` (floor). */
export function tickAt(time: number): Tick | null {
  const i = indexAt(time);
  return i >= 0 ? traceState.trace.ticks[i] : null;
}

/** The active tick plus the one before it in the same movement (for ramp checks). */
export function tickPairAt(time: number): { cur: Tick | null; prev: Tick | null } {
  const ticks = traceState.trace.ticks;
  const i = indexAt(time);
  if (i < 0) return { cur: null, prev: null };
  const cur = ticks[i];
  let prev: Tick | null = null;
  if (i > 0 && ticks[i - 1].mi === cur.mi) prev = ticks[i - 1];
  return { cur, prev };
}

/** The movement active at `time`, via the active tick's movement index. */
export function movementAt(time: number): Movement | null {
  const tk = tickAt(time);
  if (!tk) return null;
  return traceState.trace.movements.find((m) => m.index === tk.mi) ?? null;
}

/** Robot heading at the last tick of a movement — the approach direction the
 *  perpendicular-line exit is measured against. */
export function movementEndHeading(index: number): number {
  const ticks = traceState.trace.ticks;
  for (let i = ticks.length - 1; i >= 0; i--) {
    if (ticks[i].mi === index) return ticks[i].h;
  }
  return 0;
}

/** All ticks belonging to a movement (used by odometry-driven overlays). */
export function movementTicks(index: number): Tick[] {
  return traceState.trace.ticks.filter((t) => t.mi === index);
}

/** First and last tick poses for a movement (used by turn overlays). */
export function movementBounds(
  index: number
): { start: { x: number; y: number; h: number }; end: { x: number; y: number; h: number } } | null {
  const ticks = traceState.trace.ticks;
  let first: Tick | null = null;
  let last: Tick | null = null;
  for (const t of ticks) {
    if (t.mi === index) {
      if (!first) first = t;
      last = t;
    }
  }
  if (!first || !last) return null;
  return {
    start: { x: first.x, y: first.y, h: first.h },
    end: { x: last.x, y: last.y, h: last.h }
  };
}
