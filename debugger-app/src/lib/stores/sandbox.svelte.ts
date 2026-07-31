// Sandbox mode state — route code text (the single source of truth), sim
// knobs (left panel), per-movement effort overrides, saved autos, selection +
// playback. Parse + simulation are memoized here so every component reads the
// same computed route.

import { parseRoute, rewriteNumber, setStructField, type ParseResult, type StructInit } from '$lib/sandbox/parse';
import { simulate, DEFAULT_KNOBS, type SimKnobs, type SimResult, type Pose, type EffortOverrides } from '$lib/sandbox/sim';
import { settings, holesToInches } from '$lib/stores/settings.svelte';
import {
  defaultTrajectory,
  splitAnchor,
  normDeg,
  round1,
  DEFAULT_TRAJ_PARAMS,
  type TrajectoryParams,
  type TrajectoryData,
  type TrajectoryMap
} from '$lib/trajectory/spline';

// re-export so components can import trajectory types from the sandbox store
export { DEFAULT_TRAJ_PARAMS };
export type { TrajectoryParams, TrajectoryData, TrajectoryMap };

const STORAGE_KEY = 'lucaslib-debugger.sandbox';

// [TRAJECTORY] markers own a bezier spline + follow settings, keyed by statement
// index (like `efforts`). Anchor 0's position is pinned to the robot's live pose
// at sim time; the stored anchors[1..] are the shape.

const DEFAULT_CODE = `// Sandbox route — real LucasLib calls, paste-ready for auton.cpp.
// Targets, headings and leads are draggable on the field; the text
// rewrites itself. Sim-only behavior lives in the left panel.
// NOTE: backward defaults to false — set 'backward = true;' to drive reversed.
// drive() ignores it: a negative distance is what reverses that one.

chassis.setPos(0, -62.5, 0);

drive(24, chassis.h, {.settleGate = 1});
face(90);
moveTo(48, -24, {.t = 3000});

BoomParams goal {.x = 24, .y = 24, .heading = 315, .lead = 0.5, .gLead = 0.3};
facePoint(goal);
boomerangTo(goal, {.settleGate = 2});

drive(-12);
`;

/** A saved auton = one .txt file in the "sandbox autons" folder. */
export interface SavedAuton {
  name: string; // = file name (without .txt)
  savedAt: number; // file mtime, ms epoch
  content: string; // full file text (params header + code)
}

interface Persisted {
  code: string;
  knobs: SimKnobs;
  /** per-movement heading-correction-effort overrides, keyed by stmt index */
  efforts: EffortOverrides;
  /** [TRAJECTORY] spline + settings, keyed by stmt index */
  trajectories: TrajectoryMap;
  /** bumped when a knob's MEANING changes, so stale values get re-defaulted */
  knobsVersion?: number;
}

// 2 = the lib's CROSS rewrite: crossR is gone and crossD flipped from "exit within
//     this distance of the ⊥ line" to "which side of it", where 0 is ARMED (line on
//     the target) and negative is off. A stored crossD of 0 meant OFF under v1, so
//     carrying it over would silently arm cross on every move.
// 3 = turns gained setR/setD settle, and extA changed from "the settle activation
//     zone" to a separate UNGATED exit. A stored extA of 10 would now hard-exit
//     every turn 10° short without waiting for it to stop.
// 4: exit conditions became t / v / d / settleGate with a speed-based settle
const KNOBS_VERSION = 4;

function load(): Persisted {
  if (typeof localStorage !== 'undefined') {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (raw) {
        const p = JSON.parse(raw) as Partial<Persisted> & { knobs?: Record<string, number | boolean> };
        const knobs: SimKnobs = { ...DEFAULT_KNOBS };
        for (const k of Object.keys(DEFAULT_KNOBS) as (keyof SimKnobs)[]) {
          const v = p.knobs?.[k];
          // knobs are numbers or booleans (omniWheels) — type-match the default
          if (typeof v === typeof DEFAULT_KNOBS[k]) (knobs as unknown as Record<string, number | boolean>)[k] = v!;
        }
        // weight moved to a 2–10 lb range — snap stale out-of-range values in
        knobs.robotWeight = Math.min(10, Math.max(2, knobs.robotWeight));
        // the EC knobs are "what an omitted field resolves to" — when their meaning
        // changes in the lib, a stale value is worse than no value
        if (p.knobsVersion !== KNOBS_VERSION) {
          knobs.ecLatT = DEFAULT_KNOBS.ecLatT;
          knobs.ecLatV = DEFAULT_KNOBS.ecLatV;
          knobs.ecLatD = DEFAULT_KNOBS.ecLatD;
          knobs.ecLatGate = DEFAULT_KNOBS.ecLatGate;
          knobs.ecTurnT = DEFAULT_KNOBS.ecTurnT;
          knobs.ecTurnV = DEFAULT_KNOBS.ecTurnV;
          knobs.ecTurnD = DEFAULT_KNOBS.ecTurnD;
          knobs.ecTurnGate = DEFAULT_KNOBS.ecTurnGate;
        }
        return {
          code: typeof p.code === 'string' && p.code.trim() ? p.code : DEFAULT_CODE,
          knobs,
          efforts: p.efforts ?? {},
          trajectories: p.trajectories ?? {}
        };
      }
    } catch {
      // corrupt — reset
    }
  }
  return { code: DEFAULT_CODE, knobs: { ...DEFAULT_KNOBS }, efforts: {}, trajectories: {} };
}

const initial = load();

export const sandbox = $state<{
  code: string;
  knobs: SimKnobs;
  efforts: EffortOverrides;
  trajectories: TrajectoryMap;
  saved: SavedAuton[];
  /** selected statement index (into parse result stmts), or null */
  selected: number | null;
  time: number; // playhead ms over the simulated route
  playing: boolean;
}>({
  code: initial.code,
  knobs: initial.knobs,
  efforts: initial.efforts,
  trajectories: initial.trajectories,
  saved: [], // populated from the "sandbox autons" folder via refreshSavedAutons()
  selected: null,
  time: 0,
  playing: false
});

// Trackwidth is NOT a knob: it tracks the robot-settings width minus 3 in
// (wheel centers sit inboard of the chassis edge).
$effect.root(() => {
  $effect(() => {
    sandbox.knobs.trackwidth = Math.max(6, holesToInches(settings.robotWidthHoles) - 3);
  });
  // Persist the working route (code + knobs + efforts). Saved autons live as
  // files in the folder, not in localStorage.
  $effect(() => {
    if (typeof localStorage === 'undefined') return;
    const data: Persisted = {
      code: sandbox.code,
      knobs: { ...sandbox.knobs },
      efforts: { ...sandbox.efforts },
      trajectories: $state.snapshot(sandbox.trajectories) as TrajectoryMap,
      knobsVersion: KNOBS_VERSION
    };
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(data));
    } catch {
      // quota — ignore
    }
  });
});

// --- memoized parse + sim ---------------------------------------------------

export interface Route {
  parsed: ParseResult;
  sim: SimResult;
}

let cacheKey = '';
let cacheVal: Route | null = null;

/** Parse + simulate the current code (memoized). Reactive: reads code+knobs+efforts. */
export function getRoute(): Route {
  const trajSnap = $state.snapshot(sandbox.trajectories);
  const key =
    sandbox.code +
    '␟' +
    JSON.stringify(sandbox.knobs) +
    '␟' +
    JSON.stringify(sandbox.efforts) +
    '␟' +
    JSON.stringify(trajSnap);
  if (key !== cacheKey || !cacheVal) {
    const parsed = parseRoute(sandbox.code);
    const sim = simulate(parsed, { ...sandbox.knobs }, { ...sandbox.efforts }, trajSnap as TrajectoryMap);
    cacheKey = key;
    cacheVal = { parsed, sim };
  }
  return cacheVal;
}

/** Interpolated robot pose at the playhead (null before setPos parses). */
export function sandboxPoseAt(time: number): Pose | null {
  const { sim } = getRoute();
  if (!sim.startPose) return null;
  let last: Pose = sim.startPose;
  for (const seg of sim.segments) {
    const ticks = seg.ticks;
    if (!ticks.length) continue;
    if (time < ticks[0].t) return last;
    if (time <= ticks[ticks.length - 1].t) {
      // binary search inside this segment
      let lo = 0;
      let hi = ticks.length - 1;
      while (hi - lo > 1) {
        const mid = (lo + hi) >> 1;
        if (ticks[mid].t <= time) lo = mid;
        else hi = mid;
      }
      const a = ticks[lo];
      const b = ticks[Math.min(lo + 1, ticks.length - 1)];
      const span = b.t - a.t;
      const u = span > 0 ? (time - a.t) / span : 0;
      let dh = b.h - a.h;
      while (dh > 180) dh -= 360;
      while (dh < -180) dh += 360;
      return { x: a.x + (b.x - a.x) * u, y: a.y + (b.y - a.y) * u, h: a.h + dh * u };
    }
    last = { x: ticks[ticks.length - 1].x, y: ticks[ticks.length - 1].y, h: ticks[ticks.length - 1].h };
  }
  return sim.endPose ?? last;
}

/** The segment whose time span contains the playhead. */
export function segmentAt(time: number) {
  const { sim } = getRoute();
  for (const seg of sim.segments) {
    if (!seg.ticks.length) continue;
    if (time <= seg.ticks[seg.ticks.length - 1].t) return seg;
  }
  return sim.segments[sim.segments.length - 1] ?? null;
}

// --- two-way sync edits -------------------------------------------------------
// All edits rewrite sandbox.code in place. Ranges come from the CURRENT parse,
// so callers must re-read getRoute() after each edit (drags do this per frame).

export function editNumber(lit: { start: number; end: number }, value: number, decimals = 1) {
  sandbox.code = rewriteNumber(sandbox.code, lit, value, decimals);
}

export function editStructField(init: StructInit, name: string, value: number, decimals = 1) {
  sandbox.code = setStructField(sandbox.code, init, name, value, decimals);
}

export function resetSandboxCode() {
  sandbox.code = DEFAULT_CODE;
  sandbox.efforts = {};
  sandbox.trajectories = {};
  sandbox.selected = null;
  sandbox.time = 0;
  sandbox.playing = false;
}

// --- [TRAJECTORY] editing -------------------------------------------------------
// The spline for a marker lives in sandbox.trajectories[stmtIndex]. Anchor 0's
// position is overwritten with the live pose at sim time, so only its tangent
// (ang/out) is user-editable; anchors[1..] are fully editable.

/** The stored trajectory for a marker, or undefined if untouched. */
export function getTrajectory(stmtIndex: number): TrajectoryData | undefined {
  return sandbox.trajectories[stmtIndex];
}

/** Create a default spline for a marker (leaving `pose`) if none exists yet. */
export function ensureTrajectory(stmtIndex: number, pose: Pose): TrajectoryData {
  let td = sandbox.trajectories[stmtIndex];
  if (!td) {
    td = { anchors: defaultTrajectory(pose), params: { ...DEFAULT_TRAJ_PARAMS } };
    sandbox.trajectories = { ...sandbox.trajectories, [stmtIndex]: td };
  }
  return sandbox.trajectories[stmtIndex];
}

export function setTrajParam<K extends keyof TrajectoryParams>(stmtIndex: number, key: K, value: number) {
  const td = sandbox.trajectories[stmtIndex];
  if (td) td.params[key] = value;
}

export function moveTrajAnchor(stmtIndex: number, ai: number, x: number, y: number) {
  const a = sandbox.trajectories[stmtIndex]?.anchors[ai];
  if (!a || ai === 0) return; // anchor 0 is pinned to the robot pose
  a.x = round1(x);
  a.y = round1(y);
}

/** Drag a tangent handle; both handles stay collinear (dragged side sets dir + its length). */
export function moveTrajHandle(stmtIndex: number, ai: number, side: 'out' | 'in', x: number, y: number) {
  const a = sandbox.trajectories[stmtIndex]?.anchors[ai];
  if (!a) return;
  const len = Math.max(3, Math.hypot(x - a.x, y - a.y));
  let ang = (Math.atan2(x - a.x, y - a.y) * 180) / Math.PI;
  if (side === 'in') ang += 180;
  a.ang = round1(normDeg(ang));
  a[side] = round1(len);
}

/** Insert an anchor on the curve at (seg, t) without changing its shape. */
export function insertTrajAnchor(stmtIndex: number, seg: number, t: number) {
  const anchors = sandbox.trajectories[stmtIndex]?.anchors;
  if (!anchors) return;
  const { outA, inB, mid } = splitAnchor(anchors[seg], anchors[seg + 1], t);
  anchors[seg].out = outA;
  anchors[seg + 1].in = inB;
  anchors.splice(seg + 1, 0, mid);
}

export function deleteTrajAnchor(stmtIndex: number, ai: number) {
  const anchors = sandbox.trajectories[stmtIndex]?.anchors;
  if (!anchors || anchors.length <= 2 || ai === 0) return; // keep a valid spline; can't drop the pinned start
  anchors.splice(ai, 1);
}

export function togglePlaySandbox() {
  const { sim } = getRoute();
  if (sandbox.time >= sim.duration) sandbox.time = 0;
  sandbox.playing = !sandbox.playing;
}

// --- route text format ---------------------------------------------------------
// A route is a plain-text file: one `//!params {…}` comment header (paste-safe —
// it's a C++ comment) carrying the name + left-panel sliders + per-movement
// efforts, then the route code verbatim. Files open + edit in any text editor.

const PARAM_PREFIX = '//!params ';

/** Serialize the current route (name + knobs + efforts + trajectories header, then code). */
export function serializeRoute(name?: string): string {
  const meta = {
    name,
    knobs: { ...sandbox.knobs },
    efforts: { ...sandbox.efforts },
    trajectories: $state.snapshot(sandbox.trajectories)
  };
  return `${PARAM_PREFIX}${JSON.stringify(meta)}\n${sandbox.code}`;
}

/** Load a route text file: pull the params header if present, then the code. */
export function loadRouteText(text: string) {
  const normalized = text.replace(/\r\n?/g, '\n');
  const nl = normalized.indexOf('\n');
  const firstLine = nl === -1 ? normalized : normalized.slice(0, nl);
  let code = normalized;
  if (firstLine.startsWith(PARAM_PREFIX)) {
    try {
      const meta = JSON.parse(firstLine.slice(PARAM_PREFIX.length)) as {
        knobs?: Record<string, number | boolean>;
        efforts?: EffortOverrides;
        trajectories?: TrajectoryMap;
      };
      if (meta.knobs) {
        for (const k of Object.keys(DEFAULT_KNOBS) as (keyof SimKnobs)[]) {
          const v = meta.knobs[k];
          if (typeof v === typeof DEFAULT_KNOBS[k]) (sandbox.knobs as unknown as Record<string, number | boolean>)[k] = v!;
        }
      }
      if (meta.efforts) sandbox.efforts = { ...meta.efforts };
      sandbox.trajectories = meta.trajectories ? structuredClone(meta.trajectories) : {};
    } catch {
      // malformed header — treat the whole thing as code
    }
    code = nl === -1 ? '' : normalized.slice(nl + 1);
  }
  sandbox.code = code;
  sandbox.selected = null;
  sandbox.time = 0;
  sandbox.playing = false;
}

// --- saved autons (folder-backed) ----------------------------------------------
// Saved autons are real .txt files in the "sandbox autons" folder, served by the
// /api/autons dev endpoint. The menu is the folder listing; "save" writes a file.

const AUTONS_API = '/api/autons';

/** Reload the saved-auton menu from the folder. */
export async function refreshSavedAutons() {
  try {
    const res = await fetch(AUTONS_API);
    if (res.ok) sandbox.saved = (await res.json()) as SavedAuton[];
  } catch {
    // dev endpoint unavailable (e.g. static build) — leave the list as-is
  }
}

/** Save the current auton to a file in the folder (name = file name). */
export async function saveAuton(name: string): Promise<void> {
  const trimmed = name.trim() || 'untitled';
  try {
    await fetch(AUTONS_API, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ name: trimmed, content: serializeRoute(trimmed) })
    });
  } catch {
    // ignore write failure
  }
  await refreshSavedAutons();
}

/** Open a saved auton into the editor (restores code + sliders for editing). */
export function loadAuton(name: string) {
  const a = sandbox.saved.find((s) => s.name === name);
  if (a) loadRouteText(a.content);
}

/** Delete a saved auton's file. */
export async function deleteAuton(name: string): Promise<void> {
  try {
    await fetch(`${AUTONS_API}?name=${encodeURIComponent(name)}`, { method: 'DELETE' });
  } catch {
    // ignore
  }
  await refreshSavedAutons();
}
