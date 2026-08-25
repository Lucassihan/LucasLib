// Trajectory Generator state — a cubic bezier spline you shape directly
// (anchors + tangent handles), the bake knobs, and the schedule they produce.
//
// The profile is NOT modelled here: buildPath converts the anchors into the
// library's own BezierSegment[] and calls sandbox/ramsete.ts's createTrajectory,
// which is the line-for-line port of src/ramsete/util/profile.cpp. So the speed
// curve on screen is the schedule the brain will bake from the same control
// points, not a second opinion about it — which is the whole point of a motion
// profile: the thing on screen is the reference the robot follows.
//
// The export is those control points (+ any non-default PathSettings), never
// baked rows, so the library stays the only thing that owns the math.
// Persists to localStorage.

import {
  createTrajectory,
  bezierCurvature,
  bezierAt,
  bezierLength,
  trajectoryDuration,
  DEFAULT_SPEEDS,
  DEFAULT_RAMSETE,
  type BezierSegment,
  type TrajPoint
} from '$lib/sandbox/ramsete';

const STORAGE_KEY = 'lucaslib-debugger.pursuit';

// One spline anchor. Handles are ALWAYS collinear (G1-smooth — followPath can't
// drive a corner anyway): `ang` is the direction of travel through the anchor
// (deg, field convention: 0 = +Y, CW+), `out`/`in` are the handle lengths on
// each side. The first anchor's ang doubles as the start heading.
export interface Anchor {
  x: number;
  y: number;
  ang: number; // OUTGOING travel direction through the anchor, deg
  out: number; // outgoing handle length, in (unused on the last anchor)
  in: number; // incoming handle length, in (unused on the first anchor)

  // --- properties of the SEGMENT LEAVING this anchor (unused on the last one) ---
  // These are BezierSegment's own fields, held on the anchor that starts the
  // segment, so they survive the export unchanged. A speed cap applies to that
  // whole segment: to slow a span INSIDE a curve, split it (insertAnchor keeps
  // the shape exactly) and cap the piece.
  segMaxVel?: number; // in/s ceiling for this segment · undefined = profile default
  segStopEnd?: boolean; // force a full stop at this segment's end
}

export interface PursuitPlanParams {
  maxVel: number; // route ceiling, in/s (PathSettings.maxVel)
  maxAccel: number; // acceleration budget, in/s² (PathSettings.maxAccel)
  velMargin: number; // fraction of maxVel the ceilings may ask for
  endSpeed: number; // in/s in the last row — bake a hot exit
}

// The library's own defaults, so an untouched path exports as the bare
// ramsete.trajectory(path) call with no PathSettings at all.
export const DEFAULT_PARAMS: PursuitPlanParams = {
  maxVel: DEFAULT_RAMSETE.maxVel,
  maxAccel: DEFAULT_RAMSETE.maxAccel,
  velMargin: DEFAULT_SPEEDS.velMargin,
  endSpeed: DEFAULT_SPEEDS.endSpeed
};

const DEFAULT_ANCHORS: Anchor[] = [
  { x: -48, y: -60, ang: 0, out: 26, in: 26 },
  { x: -36, y: 2, ang: 38, out: 22, in: 22 },
  { x: 14, y: 30, ang: 68, out: 20, in: 20 },
  { x: 52, y: 44, ang: 52, out: 18, in: 18 }
];

/**
 * One editor tab — its own path, its own "matches disk" signature, its own
 * undo/redo. The same document model the sandbox uses (sandbox.svelte.ts's
 * TabState), so the two workspaces save, open and close files identically:
 * anchors/params are copied out of/into the live pursuit.anchors/params on
 * switch (stashActiveTab / hydrateActiveTab), while cleanSig/undo/redo live
 * ONLY on the tab.
 */
export interface PathTabState {
  id: string; // stable {#each} key
  name: string | null; // the saved file this tab is open as; null = untitled
  anchors: Anchor[];
  params: PursuitPlanParams;
  cleanSig: string; // this tab's own "matches disk" signature, see tabSignature()
  undo: Snapshot[]; // this tab's own history
  redo: Snapshot[];
}

interface PersistedTab {
  name: string | null;
  anchors: Anchor[];
  params: PursuitPlanParams;
  /** persisted VERBATIM, never recomputed from the anchors — recomputing
   *  would launder every tab clean on reload and the unsaved dot would never
   *  survive one (sandbox.svelte.ts's PersistedTab.cleanSig, same reason). */
  cleanSig: string;
}

interface Persisted {
  tabs: PersistedTab[];
  activeTab: number;
  /** pre-tabs era — one document per workspace, migrated on load */
  anchors?: Anchor[];
  params?: PursuitPlanParams;
  name?: string;
  /** pre-bezier era — plain waypoints, migrated on load */
  waypoints?: { x: number; y: number }[];
}

const D2R = Math.PI / 180;
const R2D = 180 / Math.PI;

/** Unit vector of a field heading (0 = +Y, CW+). */
function dir(angDeg: number): { x: number; y: number } {
  return { x: Math.sin(angDeg * D2R), y: Math.cos(angDeg * D2R) };
}

const dist = (a: { x: number; y: number }, b: { x: number; y: number }) => Math.hypot(b.x - a.x, b.y - a.y);
const norm = (deg: number) => ((deg % 360) + 360) % 360;
const round = (v: number) => Math.round(v * 10) / 10;

/** Drop fields left behind by the removed mid-path-turn and handle-lock features. */
function migrateAnchor(a: Anchor): Anchor {
  const rest = { ...a } as Anchor & { turn?: unknown; inAng?: unknown; lock?: unknown };
  delete rest.turn;
  delete rest.inAng;
  delete rest.lock;
  return rest;
}

/**
 * Carry the old per-ANCHOR speed cap onto the segment it now belongs to.
 * `vCap` pinned the speed of whatever samples landed within 1.5" of the anchor
 * — a point cap with no equivalent in BezierSegment, so it could never have
 * survived an export. A cap of 0 was a full stop, which IS expressible: it is
 * `.stopEnd` on the segment that ARRIVES there, i.e. anchor i-1's. Any other
 * cap becomes the ceiling of the segment leaving the anchor.
 *
 * Needs the whole array for that i-1, which is why it is not in migrateAnchor.
 */
function migrateCaps(anchors: Anchor[]): Anchor[] {
  const out = anchors.map((a) => ({ ...a }) as Anchor & { vCap?: number });
  for (let i = 0; i < out.length; i++) {
    const cap = out[i].vCap;
    if (cap === undefined) continue;
    if (cap <= 0) {
      const arriving = i > 0 ? out[i - 1] : out[i];
      arriving.segStopEnd = true;
    } else if (i < out.length - 1) {
      out[i].segMaxVel = cap;
    }
    delete out[i].vCap;
  }
  return out;
}

/** Auto-smooth anchors through plain points (tangent = neighbor chord). */
function anchorsThrough(pts: { x: number; y: number }[]): Anchor[] {
  return pts.map((p, i) => {
    const prev = pts[Math.max(0, i - 1)];
    const next = pts[Math.min(pts.length - 1, i + 1)];
    const ang = norm(Math.atan2(next.x - prev.x, next.y - prev.y) * R2D);
    const inLen = i > 0 ? dist(pts[i - 1], p) * 0.35 : 15;
    const outLen = i < pts.length - 1 ? dist(p, pts[i + 1]) * 0.35 : 15;
    return { x: p.x, y: p.y, ang, out: round(outLen), in: round(inLen) };
  });
}

/** Anchors out of an untrusted blob (a persisted tab, or a file), or null when
 *  there aren't two usable ones. Spread-preserves optional fields so the
 *  segment properties survive; only the five core numbers must be finite. */
function readAnchors(raw: unknown): Anchor[] | null {
  if (!Array.isArray(raw) || raw.length < 2) return null;
  const kept = (raw as Anchor[]).filter(
    (a) => a && [a.x, a.y, a.ang, a.out, a.in].every((n) => typeof n === 'number' && isFinite(n))
  );
  if (kept.length < 2) return null;
  const anchors = migrateCaps(kept.map((a) => migrateAnchor({ ...a })));
  dropStrandedSegProps(anchors); // clears one an older build could have stranded
  return anchors;
}

function makeId(): string {
  return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
}

/** A tab's "does this match what was last saved/opened" fingerprint — the same
 *  role sandbox.svelte.ts's tabSignature plays, over the only two things a
 *  path file carries. */
function tabSignature(anchors: Anchor[], params: PursuitPlanParams): string {
  return JSON.stringify(anchors) + '␟' + JSON.stringify(params);
}

/** A brand-new untitled tab on the starter path — clean the moment it's
 *  created, empty undo/redo (nothing to undo into yet). */
function makePathTab(): PathTabState {
  const anchors = DEFAULT_ANCHORS.map((a) => ({ ...a }));
  const params = { ...DEFAULT_PARAMS };
  return {
    id: makeId(),
    name: null,
    anchors,
    params,
    cleanSig: tabSignature(anchors, params),
    undo: [],
    redo: []
  };
}

function tabFrom(name: string | null, anchors: Anchor[], params: PursuitPlanParams, cleanSig?: string): PathTabState {
  return {
    id: makeId(),
    name,
    anchors,
    params,
    cleanSig: cleanSig ?? tabSignature(anchors, params),
    undo: [],
    redo: []
  };
}

function load(): { tabs: PathTabState[]; activeTab: number } {
  if (typeof localStorage !== 'undefined') {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (raw) {
        const p = JSON.parse(raw) as Partial<Persisted>;
        if (Array.isArray(p.tabs) && p.tabs.length) {
          const tabs = p.tabs
            .map((pt) => {
              const anchors = readAnchors(pt?.anchors);
              if (!anchors) return null;
              return tabFrom(
                typeof pt?.name === 'string' ? pt.name : null,
                anchors,
                { ...DEFAULT_PARAMS, ...(pt?.params ?? {}) },
                // A blob written before this field existed has no signature,
                // and the safe fallback is clean rather than a fabricated dot.
                typeof pt?.cleanSig === 'string' ? pt.cleanSig : undefined
              );
            })
            .filter((t): t is PathTabState => t !== null);
          if (tabs.length) {
            const activeTab = typeof p.activeTab === 'number' ? Math.min(Math.max(0, p.activeTab), tabs.length - 1) : 0;
            return { tabs, activeTab };
          }
        }
        // pre-tabs single document (or the pre-bezier waypoint format before
        // that) — becomes the one open tab. Its old name was always a string
        // defaulting to 'path', so that exact value reads as "never named".
        const anchors = readAnchors(p.anchors) ?? (Array.isArray(p.waypoints) && p.waypoints.length >= 2 ? anchorsThrough(p.waypoints) : null);
        if (anchors) {
          const name = typeof p.name === 'string' && p.name.trim() && p.name !== 'path' ? p.name : null;
          return { tabs: [tabFrom(name, anchors, { ...DEFAULT_PARAMS, ...(p.params ?? {}) })], activeTab: 0 };
        }
      }
    } catch {
      // corrupt — reset
    }
  }
  return { tabs: [makePathTab()], activeTab: 0 };
}

const initial = load();

export const pursuit = $state<{
  tabs: PathTabState[];
  activeTab: number;
  /** the ACTIVE tab's path, mirrored out of tabs[activeTab] — everything else
   *  in the app reads and writes these, exactly as it did before tabs. */
  anchors: Anchor[];
  params: PursuitPlanParams;
  saved: SavedPath[];
  selected: number | null; // selected anchor index, or null
  /** the ACTIVE tab's file name, or null for untitled — kept in sync with
   *  tabs[activeTab].name by the effect below. */
  openPath: string | null;
  /** true once the active tab's path differs from its last save or open. */
  dirty: boolean;
}>({
  tabs: initial.tabs,
  activeTab: initial.activeTab,
  anchors: initial.tabs[initial.activeTab].anchors,
  params: { ...initial.tabs[initial.activeTab].params },
  saved: [], // populated from the "trajectory paths" folder via refreshSavedPaths()
  selected: null,
  openPath: initial.tabs[initial.activeTab].name,
  dirty: false
});

// Persist the whole workspace: every tab's own path, plus which one is focused.
// Saved paths live as files in the folder, not in localStorage — this is just
// enough for a reload to restore the tab strip as it was. Only the ACTIVE slot
// reads the live mirror; stashActiveTab does the write-through when it matters.
$effect.root(() => {
  $effect(() => {
    if (typeof localStorage === 'undefined') return;
    const activeTab = pursuit.activeTab;
    const data: Persisted = {
      tabs: pursuit.tabs.map((t, i) =>
        i === activeTab
          ? { name: t.name, anchors: pursuit.anchors.map((a) => ({ ...a })), params: { ...pursuit.params }, cleanSig: t.cleanSig }
          : { name: t.name, anchors: t.anchors.map((a) => ({ ...a })), params: { ...t.params }, cleanSig: t.cleanSig }
      ),
      activeTab
    };
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(data));
    } catch {
      // quota — ignore
    }
  });
});

// --- dirty tracking (per-tab) --------------------------------------------------
// "Dirty" = the ACTIVE tab's path differs from the signature taken at its last
// save or open (PathTabState.cleanSig). Its own effect rather than folded into
// the persist effect above: that one fires on every drag too, so setting dirty
// there would flip it on merely by OPENING a file.

/** Mark the active tab's path as matching what's on disk — called by the save
 *  flows (a freshly created or opened tab is already clean). */
function markClean() {
  const tab = pursuit.tabs[pursuit.activeTab];
  if (tab) tab.cleanSig = tabSignature(pursuit.anchors, pursuit.params);
  pursuit.dirty = false;
}

$effect.root(() => {
  $effect(() => {
    const tab = pursuit.tabs[pursuit.activeTab];
    pursuit.dirty = tab ? tabSignature(pursuit.anchors, pursuit.params) !== tab.cleanSig : false;
  });
  $effect(() => {
    pursuit.openPath = pursuit.tabs[pursuit.activeTab]?.name ?? null;
  });
});

/** Whether tab `i` differs from its own last-saved signature — drives each
 *  tab's unsaved dot. The active tab reads the live mirror (its own stored
 *  anchors are stale until the persist effect catches up); every other tab
 *  reads its own fields, since nothing else keeps them live. */
export function isTabDirty(i: number): boolean {
  const t = pursuit.tabs[i];
  if (!t) return false;
  if (i === pursuit.activeTab) return pursuit.dirty;
  return tabSignature(t.anchors, t.params) !== t.cleanSig;
}

// --- tab management ------------------------------------------------------------

/** Copy the live mirror into its own tab record. The persist effect does this
 *  too, but switching needs it synchronously and before activeTab moves. */
function stashActiveTab(): void {
  const t = pursuit.tabs[pursuit.activeTab];
  if (!t) return;
  t.anchors = pursuit.anchors.map((a) => ({ ...a }));
  t.params = { ...pursuit.params };
}

/** Mirror tabs[activeTab] into the live buffer everything else reads. */
function hydrateActiveTab(): void {
  const t = pursuit.tabs[pursuit.activeTab];
  if (!t) return;
  pursuit.anchors = t.anchors.map((a) => ({ ...a }));
  pursuit.params = { ...t.params };
}

/** Selection and playback mean nothing carried over from another path. */
function resetTransientUiState(): void {
  pursuit.selected = null;
  playback.t = 0;
  playback.playing = false;
}

/** Switch the focused tab: stash the outgoing path, move activeTab, hydrate
 *  the incoming one. Undo/redo and cleanSig live on each tab already, which
 *  is what makes history per-tab for free. */
export function switchTab(index: number): void {
  if (index < 0 || index >= pursuit.tabs.length || index === pursuit.activeTab) return;
  stashActiveTab();
  pursuit.activeTab = index;
  hydrateActiveTab();
  resetTransientUiState();
}

/** Close tab `i`. Does NOT confirm — the caller checks isTabDirty and arms
 *  first. Closing the last tab leaves one fresh untitled tab. */
export function closeTab(i: number): void {
  if (!pursuit.tabs[i]) return;
  const wasActive = i === pursuit.activeTab;
  pursuit.tabs.splice(i, 1);
  if (pursuit.tabs.length === 0) pursuit.tabs.push(makePathTab());
  if (wasActive) {
    pursuit.activeTab = Math.min(i, pursuit.tabs.length - 1);
    hydrateActiveTab();
    resetTransientUiState();
  } else if (i < pursuit.activeTab) {
    pursuit.activeTab--;
  }
}

/** New — open a fresh untitled tab on the starter path and focus it. Tabs are
 *  additive: New never touches any other tab's path. */
export function newPath(): void {
  stashActiveTab();
  pursuit.tabs.push(makePathTab());
  pursuit.activeTab = pursuit.tabs.length - 1;
  hydrateActiveTab();
  resetTransientUiState();
}

// --- view options (not part of the path — editor UI prefs, persisted apart) ----
const VIEW_KEY = 'lucaslib-debugger.pursuit-view';

export interface ViewOpts {
  snap: number; // grid snap size, inches: 0 = off · 24 tile · 12 half · 6 quarter
  angleSnap: number; // tangent-handle angle snap, degrees: 0 = off · 15 · 45 · 90
  showDots: boolean; // draw the baked PathPoint dots on the field
  comb: boolean; // curvature comb along the path
  followCam: boolean; // camera eases toward the robot during playback
  bounds: boolean; // highlight off-field anchors / spans
  elements: boolean; // overlay game field-element coordinates
  unit: 'in/s' | 'ft/s' | '%'; // speed readout unit
  overlay: 'off' | 'k' | 'a'; // velocity-strip secondary trace
  speed: number; // playback rate multiplier
  loop: boolean; // loop playback
}

const DEFAULT_VIEW: ViewOpts = {
  snap: 0,
  angleSnap: 0,
  showDots: false,
  comb: false,
  followCam: false,
  bounds: true,
  elements: false,
  unit: 'in/s',
  overlay: 'k',
  speed: 1,
  loop: false
};

function loadView(): ViewOpts {
  if (typeof localStorage !== 'undefined') {
    try {
      const raw = localStorage.getItem(VIEW_KEY);
      if (raw) return { ...DEFAULT_VIEW, ...(JSON.parse(raw) as Partial<ViewOpts>) };
    } catch {
      // corrupt — reset
    }
  }
  return { ...DEFAULT_VIEW };
}

export const viewOpts = $state<ViewOpts>(loadView());

/** Shared playback head (seconds) so the velocity strip and field stay in sync. */
export const playback = $state({ t: 0, playing: false });

$effect.root(() => {
  $effect(() => {
    if (typeof localStorage === 'undefined') return;
    try {
      localStorage.setItem(VIEW_KEY, JSON.stringify({ ...viewOpts }));
    } catch {
      // quota — ignore
    }
  });
});

// --- undo / redo ---------------------------------------------------------------
// A snapshot is the editable path (anchors + params); the file name is not in it,
// so undo can't rename the document out from under the tab strip. The stacks live
// ON the active tab (PathTabState.undo/redo), which is what makes history per-tab
// — switching away and back leaves each path's history where it was. Discrete
// edits push before mutating; rapid same-key edits (a slider being dragged,
// arrow-key nudges) coalesce into one entry so one gesture = one undo step.

interface Snapshot {
  anchors: Anchor[];
  params: PursuitPlanParams;
}

let lastKey = '';
let lastAt = 0;
const HIST_CAP = 120;
const COALESCE_MS = 700;

/** Reactive flags for the toolbar buttons. */
export const history = $state({ canUndo: false, canRedo: false });

$effect.root(() => {
  $effect(() => {
    const tab = pursuit.tabs[pursuit.activeTab];
    history.canUndo = (tab?.undo.length ?? 0) > 0;
    history.canRedo = (tab?.redo.length ?? 0) > 0;
  });
});

function snap(): Snapshot {
  return { anchors: pursuit.anchors.map((a) => ({ ...a })), params: { ...pursuit.params } };
}
function apply(s: Snapshot) {
  pursuit.anchors = s.anchors.map((a) => ({ ...a }));
  pursuit.params = { ...s.params };
  if (pursuit.selected !== null && pursuit.selected >= pursuit.anchors.length) pursuit.selected = null;
}

/**
 * Record the current state onto the undo stack before a change. Pass a
 * `coalesceKey` for continuous edits (slider drag, arrow nudge) so a burst of
 * same-key pushes within {@link COALESCE_MS} collapses into a single step.
 */
export function pushHistory(coalesceKey = '') {
  const tab = pursuit.tabs[pursuit.activeTab];
  if (!tab) return;
  const now = Date.now();
  if (coalesceKey && coalesceKey === lastKey && now - lastAt < COALESCE_MS && tab.undo.length) {
    lastAt = now;
    tab.redo = [];
    return;
  }
  tab.undo.push(snap());
  if (tab.undo.length > HIST_CAP) tab.undo.shift();
  tab.redo = [];
  lastKey = coalesceKey;
  lastAt = now;
}

export function undo() {
  const tab = pursuit.tabs[pursuit.activeTab];
  if (!tab?.undo.length) return;
  tab.redo.push(snap());
  apply(tab.undo.pop()!);
  lastKey = '';
}
export function redo() {
  const tab = pursuit.tabs[pursuit.activeTab];
  if (!tab?.redo.length) return;
  tab.undo.push(snap());
  apply(tab.redo.pop()!);
  lastKey = '';
}

/** Set a shaping/follow param, coalescing rapid changes to the same knob. */
export function setParam<K extends keyof PursuitPlanParams>(k: K, v: PursuitPlanParams[K]) {
  pushHistory('param:' + k);
  pursuit.params[k] = v;
}

// --- speed → color -------------------------------------------------------------
// Lives in $lib/trace/speedRamp so the Replay's driven path can share the exact
// ramp the generator draws with. Re-exported here because every call site in
// this workspace already reaches for it through the store.
export { speedColor, speedRange } from '$lib/trace/speedRamp';

// --- the spline ---------------------------------------------------------------

export interface Pt {
  x: number;
  y: number;
}

/** The four cubic control points of segment i (anchors i -> i+1). */
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

/** The whole spline as the library's own type — what gets exported, and what
 *  the bake below runs on. Segment i is authored on anchor i. */
export function pathSegments(anchors: Anchor[]): BezierSegment[] {
  const segs: BezierSegment[] = [];
  for (let i = 0; i < anchors.length - 1; i++) {
    const [a, b, c, d] = segControls(anchors[i], anchors[i + 1]);
    const seg: BezierSegment = { a, b, c, d };
    if (anchors[i].segMaxVel !== undefined) seg.maxVel = anchors[i].segMaxVel;
    if (anchors[i].segStopEnd) seg.stopEnd = true;
    segs.push(seg);
  }
  return segs;
}

/** The PathSettings the bake runs with — the knobs, resolved the same way
 *  resolveConfig() does on the brain. */
function profileConfig(params: PursuitPlanParams) {
  return {
    maxVel: params.maxVel,
    maxAccel: params.maxAccel,
    velMargin: params.velMargin,
    endSpeed: params.endSpeed
  };
}

// --- the planning pipeline ------------------------------------------------------
// There isn't one any more, and that is the point. buildPath hands the control
// points to the library port's createTrajectory and then only REPACKAGES what
// comes back: `points` is the schedule's own rows, `dense` is a fine geometric
// sampling for drawing whose speeds are read back out of those rows. Nothing
// here decides a speed.
//
// The library's ceilings (profile.cpp) are wheel saturation, the per-segment
// authored .maxVel, and the two acceleration sweeps. There is no traction /
// no-slip term in either place any more.

export interface BakedPoint {
  x: number;
  y: number;
  v: number; // planned speed magnitude, in/s (the row's |v|)
  s: number; // cumulative arc length, inches
}

/** A dense spline sample: remembers where it came from (for insert-on-path) and
 *  carries the full-resolution arc length, curvature, and planned speed so the
 *  on-field line can be both smooth and speed-coloured without the coarse export. */
export interface DenseSample extends Pt {
  seg: number;
  t: number;
  s: number; // cumulative arc length, inches
  k: number; // exact curvature magnitude reference (signed), 1/in
  v: number; // planned speed at this sample, in/s
}

export interface Plan {
  points: BakedPoint[];
  dense: DenseSample[];
  total: number; // path length, inches
  traj: TrajPoint[]; // the schedule itself — real timestamps, headings and ω
  duration: number; // seconds, straight off the last row
}

const EMPTY_PLAN: Plan = { points: [], dense: [], total: 0, traj: [], duration: 0 };

export function buildPath(anchors: Anchor[], params: PursuitPlanParams): Plan {
  if (anchors.length < 2) return EMPTY_PLAN;

  const segs = pathSegments(anchors);

  // THE bake — the same function the sandbox runs and the same math the brain
  // runs. Everything below is presentation.
  const traj = createTrajectory(segs, profileConfig(params), DEFAULT_RAMSETE);
  if (traj.length < 2) return EMPTY_PLAN;

  // Rows carry time, not arc, so walk them once for the cumulative distance the
  // strip and the field colouring index by. Joints emit two rows at the same
  // place (see profile.cpp), which simply contribute a zero-length step.
  const points: BakedPoint[] = [{ x: traj[0].x, y: traj[0].y, v: Math.abs(traj[0].v), s: 0 }];
  for (let i = 1; i < traj.length; i++) {
    const s = points[i - 1].s + Math.hypot(traj[i].x - traj[i - 1].x, traj[i].y - traj[i - 1].y);
    points.push({ x: traj[i].x, y: traj[i].y, v: Math.abs(traj[i].v), s });
  }
  const total = points[points.length - 1].s;

  // A finer geometric sampling for the drawn line and the curvature comb. The
  // positions and curvature come from the spline; the SPEED is read back out of
  // the schedule above, so the smooth line and the coarse rows never disagree.
  const dense: DenseSample[] = [];
  for (let i = 0; i < segs.length; i++) {
    const n = Math.min(400, Math.max(16, Math.ceil(bezierLength(segs[i], 50) / 0.5)));
    for (let k = i === 0 ? 0 : 1; k <= n; k++) {
      const t = k / n;
      const p = bezierAt(segs[i], t);
      dense.push({ x: p.x, y: p.y, seg: i, t, s: 0, k: bezierCurvature(segs[i], t), v: 0 });
    }
  }
  for (let i = 1; i < dense.length; i++) {
    dense[i].s = dense[i - 1].s + dist(dense[i - 1], dense[i]);
  }
  let row = 1;
  for (const d of dense) {
    while (row < points.length - 1 && points[row].s < d.s) row++;
    const span = points[row].s - points[row - 1].s;
    const u = span > 1e-9 ? (d.s - points[row - 1].s) / span : 0;
    d.v = points[row - 1].v + (points[row].v - points[row - 1].v) * u;
  }

  return { points, dense, total, traj, duration: trajectoryDuration(traj) };
}

/** Total traversal time of a baked plan (seconds) — the schedule's own last
 *  timestamp, not an estimate reconstructed from speeds. */
export function planDuration(plan: Plan): number {
  return plan.duration;
}

// memoized plan — recomputed only when anchors or bake params change
let cacheKey = '';
let cacheVal: Plan = EMPTY_PLAN;

/** The baked path for the current spline + params (memoized, reactive). */
export function getPlan(): Plan {
  const key = JSON.stringify(pursuit.anchors) + '|' + JSON.stringify(pursuit.params);
  if (key !== cacheKey) {
    cacheVal = buildPath(pursuit.anchors, pursuit.params);
    cacheKey = key;
  }
  return cacheVal;
}

// --- spline editing -------------------------------------------------------------

export function moveAnchor(i: number, x: number, y: number) {
  const a = pursuit.anchors[i];
  a.x = round(x);
  a.y = round(y);
}

/**
 * Drag one of an anchor's tangent handles. Both handles stay collinear: the
 * dragged side sets the shared direction and its own length; the other side
 * keeps its length.
 */
export function moveHandle(i: number, side: 'out' | 'in', x: number, y: number) {
  const a = pursuit.anchors[i];
  const vx = x - a.x;
  const vy = y - a.y;
  const len = Math.max(3, Math.hypot(vx, vy));
  let ang = Math.atan2(vx, vy) * R2D;
  if (side === 'in') ang += 180; // the in-handle points backward along the path
  a.ang = round(norm(ang));
  a[side] = round(len);
}

/** Set exact anchor fields from typed input (coordinate / heading / handle boxes). */
export function editAnchor(i: number, patch: Partial<Anchor>) {
  const a = pursuit.anchors[i];
  if (a === undefined) return;
  pushHistory('edit:' + i);
  if (patch.x !== undefined) a.x = round(patch.x);
  if (patch.y !== undefined) a.y = round(patch.y);
  if (patch.ang !== undefined) a.ang = round(norm(patch.ang));
  if (patch.out !== undefined) a.out = round(Math.max(3, patch.out));
  if (patch.in !== undefined) a.in = round(Math.max(3, patch.in));
}

/** Snap a coordinate to the nearest multiple of `grid` (0 = no snap). */
export function snapCoord(v: number, grid: number): number {
  return grid > 0 ? Math.round(v / grid) * grid : v;
}

/** Nudge the selected anchor by (dx,dy) inches; coalesces arrow-key bursts. */
export function nudgeSelected(dx: number, dy: number) {
  const i = pursuit.selected;
  if (i === null) return;
  const a = pursuit.anchors[i];
  if (!a) return;
  pushHistory('nudge:' + i);
  a.x = round(a.x + dx);
  a.y = round(a.y + dy);
}

/**
 * Set (or clear, with null) the speed ceiling of the segment LEAVING anchor i —
 * BezierSegment.maxVel, exported verbatim. The last anchor starts no segment,
 * so it has none. To slow a span inside one curve, insertAnchor() at both ends
 * of the span first: the split reproduces the curve exactly and an ordinary
 * joint costs nothing, so the middle piece can be capped on its own.
 */
export function setSegmentCap(i: number, cap: number | null) {
  const a = pursuit.anchors[i];
  if (!a || i >= pursuit.anchors.length - 1) return;
  pushHistory('cap:' + i);
  if (cap === null) delete a.segMaxVel;
  else a.segMaxVel = Math.max(0, round(cap));
}

/** Force a full stop at the end of the segment leaving anchor i. */
export function setSegmentStop(i: number, stop: boolean) {
  const a = pursuit.anchors[i];
  if (!a || i >= pursuit.anchors.length - 1) return;
  pushHistory('stop:' + i);
  if (stop) a.segStopEnd = true;
  else delete a.segStopEnd;
}

/** Append an anchor at the end of the spline, tangents along the closing chord. */
export function appendAnchor(x: number, y: number) {
  pushHistory();
  const last = pursuit.anchors[pursuit.anchors.length - 1];
  const chord = Math.max(6, dist(last, { x, y }));
  const ang = norm(Math.atan2(x - last.x, y - last.y) * R2D);
  last.out = round(chord * 0.35);
  pursuit.anchors.push({ x: round(x), y: round(y), ang: round(ang), out: round(chord * 0.35), in: round(chord * 0.35) });
  pursuit.selected = pursuit.anchors.length - 1;
}

/** Append a new anchor continuing past the end along the path's exit direction. */
export function addAnchorEnd() {
  const n = pursuit.anchors.length;
  const last = pursuit.anchors[n - 1];
  const prev = pursuit.anchors[n - 2] ?? last;
  const d = dir(last.ang);
  const step = Math.max(18, dist(prev, last) * 0.8);
  appendAnchor(last.x + d.x * step, last.y + d.y * step);
}

/** Insert an anchor at the midpoint of the selected segment (else the middle one). */
export function insertMidpoint() {
  const n = pursuit.anchors.length;
  let seg = pursuit.selected !== null && pursuit.selected < n - 1 ? pursuit.selected : Math.floor((n - 1) / 2);
  seg = Math.max(0, Math.min(n - 2, seg));
  insertAnchor(seg, 0.5);
}

/**
 * Insert an anchor ON the spline at (segment, t) without changing its shape —
 * de Casteljau split. The neighbors keep their tangent directions; only their
 * handle lengths shrink to the split's control points.
 */
export function insertAnchor(seg: number, t: number) {
  pushHistory();
  const a = pursuit.anchors[seg];
  const b = pursuit.anchors[seg + 1];
  const [q0, q1, q2, q3] = segControls(a, b);
  const lerp = (p: Pt, q: Pt): Pt => ({ x: p.x + (q.x - p.x) * t, y: p.y + (q.y - p.y) * t });
  const l1 = lerp(q0, q1);
  const h = lerp(q1, q2);
  const r2 = lerp(q2, q3);
  const l2 = lerp(l1, h);
  const r1 = lerp(h, r2);
  const m = lerp(l2, r1);
  const ang = norm(Math.atan2(r1.x - l2.x, r1.y - l2.y) * R2D);
  a.out = round(dist(q0, l1));
  b.in = round(dist(r2, q3));
  pursuit.anchors.splice(seg + 1, 0, {
    x: round(m.x),
    y: round(m.y),
    ang: round(ang),
    in: round(Math.max(3, dist(l2, m))),
    out: round(Math.max(3, dist(m, r1)))
  });
  pursuit.selected = seg + 1;
}

/**
 * Clear segment properties left on the LAST anchor. It starts no segment, so a
 * cap or stop sitting there exports nothing and setSegmentCap/setSegmentStop
 * refuse to touch it — the field would draw a cap ring with no way to clear it.
 * Deleting or reversing can strand one there, so both call this afterwards.
 */
function dropStrandedSegProps(anchors: Anchor[]) {
  const last = anchors[anchors.length - 1];
  if (!last) return;
  delete last.segMaxVel;
  delete last.segStopEnd;
}

export function deleteAnchor(i: number) {
  if (pursuit.anchors.length <= 2) return; // keep a valid path
  pushHistory();
  pursuit.anchors.splice(i, 1);
  dropStrandedSegProps(pursuit.anchors);
  pursuit.selected = null;
}

// --- transforms ---------------------------------------------------------------

export type MirrorAxis = 'vertical' | 'horizontal' | 'diag' | 'antidiag' | 'rotate';

/**
 * Mirror the whole path across a field axis (heading 0 = +Y, CW+). Positions and
 * travel directions transform together; handle lengths are unchanged. Diagonals
 * cover VEX's diagonally/rotationally symmetric fields, not just L↔R.
 */
export function mirrorPath(axis: MirrorAxis) {
  pushHistory();
  pursuit.anchors = pursuit.anchors.map((a) => {
    let x = a.x;
    let y = a.y;
    let ang = a.ang;
    switch (axis) {
      case 'vertical': // reflect across x = 0 (swap alliance left↔right)
        x = -a.x;
        ang = -a.ang;
        break;
      case 'horizontal': // reflect across y = 0
        y = -a.y;
        ang = 180 - a.ang;
        break;
      case 'diag': // reflect across y = x
        x = a.y;
        y = a.x;
        ang = 90 - a.ang;
        break;
      case 'antidiag': // reflect across y = -x
        x = -a.y;
        y = -a.x;
        ang = 270 - a.ang;
        break;
      case 'rotate': // 180° rotation about the field centre
        x = -a.x;
        y = -a.y;
        ang = a.ang + 180;
        break;
    }
    return { ...a, x: round(x), y: round(y), ang: round(norm(ang)) };
  });
}

/**
 * Reverse travel direction: flip anchor order, spin each 180°, swap handles.
 *
 * Caps and stops ride on the anchor that STARTS their segment, so they cannot
 * come along with it: geometric segment j is segment n-2-j once reversed, and a
 * stopEnd moves back one further still, onto whatever segment now ARRIVES at the
 * anchor the stop belongs to. A stop at the old finish has nowhere to land —
 * the reversed path starts there, already at rest.
 */
export function reversePath() {
  pushHistory();
  const old = pursuit.anchors;
  const n = old.length;
  const flipped = old
    .slice()
    .reverse()
    .map((a) => {
      const b: Anchor = { ...a, ang: round(norm(a.ang + 180)), in: a.out, out: a.in };
      delete b.segMaxVel;
      delete b.segStopEnd;
      return b;
    });
  for (let j = 0; j < n - 1; j++) {
    if (old[j].segMaxVel !== undefined) flipped[n - 2 - j].segMaxVel = old[j].segMaxVel;
    if (old[j].segStopEnd && n - 3 - j >= 0) flipped[n - 3 - j].segStopEnd = true;
  }
  pursuit.anchors = flipped;
  pursuit.selected = null;
}

// --- shapes: templates + 3-point arc fitting ----------------------------------

/** Circle through three points (circumcircle), or null if they're collinear. */
function circleFrom3(p1: Pt, p2: Pt, p3: Pt): { cx: number; cy: number; r: number } | null {
  const d = 2 * (p1.x * (p2.y - p3.y) + p2.x * (p3.y - p1.y) + p3.x * (p1.y - p2.y));
  if (Math.abs(d) < 1e-6) return null;
  const s1 = p1.x * p1.x + p1.y * p1.y;
  const s2 = p2.x * p2.x + p2.y * p2.y;
  const s3 = p3.x * p3.x + p3.y * p3.y;
  const cx = (s1 * (p2.y - p3.y) + s2 * (p3.y - p1.y) + s3 * (p1.y - p2.y)) / d;
  const cy = (s1 * (p3.x - p2.x) + s2 * (p1.x - p3.x) + s3 * (p2.x - p1.x)) / d;
  return { cx, cy, r: Math.hypot(p1.x - cx, p1.y - cy) };
}

/**
 * Bezier anchors for the circular arc P1 → P3 that passes through P2 (Fusion's
 * 3-point arc). Exact tangents (⊥ radius) + the `4/3·tan(θ/4)·R` handle length
 * per ≤90° sub-arc, so the spline reproduces the circle. Collinear points → a
 * straight two-anchor segment.
 */
export function arcAnchors(p1: Pt, p2: Pt, p3: Pt): Anchor[] {
  const c = circleFrom3(p1, p2, p3);
  if (!c) {
    const ang = norm(Math.atan2(p3.x - p1.x, p3.y - p1.y) * R2D);
    const h = round(Math.max(6, dist(p1, p3) * 0.35));
    return [
      { x: round(p1.x), y: round(p1.y), ang, out: h, in: h },
      { x: round(p3.x), y: round(p3.y), ang, out: h, in: h }
    ];
  }
  const ang1 = Math.atan2(p1.y - c.cy, p1.x - c.cx);
  const ang3 = Math.atan2(p3.y - c.cy, p3.x - c.cx);
  const ang2 = Math.atan2(p2.y - c.cy, p2.x - c.cx);
  // sweep from ang1 to ang3 the way that passes through ang2
  const twoPi = Math.PI * 2;
  const ccw = norm2(ang2 - ang1) < norm2(ang3 - ang1); // is ang2 reached before ang3 going CCW?
  function norm2(a: number): number {
    return ((a % twoPi) + twoPi) % twoPi;
  }
  let sweep = ccw ? norm2(ang3 - ang1) : -norm2(ang1 - ang3);
  if (sweep === 0) sweep = ccw ? twoPi : -twoPi;
  const segs = Math.max(1, Math.ceil(Math.abs(sweep) / (Math.PI / 2)));
  const step = sweep / segs;
  const handle = round(Math.max(3, (4 / 3) * Math.tan(Math.abs(step) / 4) * c.r));
  const out: Anchor[] = [];
  for (let k = 0; k <= segs; k++) {
    const a = ang1 + step * k;
    const x = c.cx + c.r * Math.cos(a);
    const y = c.cy + c.r * Math.sin(a);
    // travel tangent (CCW: (-sin,cos)); flip for CW; convert to field heading
    const tx = -Math.sin(a) * Math.sign(step);
    const ty = Math.cos(a) * Math.sign(step);
    out.push({ x: round(x), y: round(y), ang: round(norm(Math.atan2(tx, ty) * R2D)), out: handle, in: handle });
  }
  return out;
}

export function loadTemplate(kind: 'straight' | 'arc' | 'scurve' | 'uturn') {
  pushHistory();
  pursuit.selected = null;
  switch (kind) {
    case 'straight':
      pursuit.anchors = [
        { x: 0, y: -48, ang: 0, out: 32, in: 32 },
        { x: 0, y: 48, ang: 0, out: 32, in: 32 }
      ];
      break;
    case 'arc':
      pursuit.anchors = arcAnchors({ x: -42, y: -24 }, { x: 0, y: 18 }, { x: 42, y: -24 });
      break;
    case 'uturn':
      pursuit.anchors = arcAnchors({ x: -22, y: -36 }, { x: 0, y: 30 }, { x: 22, y: -36 });
      break;
    case 'scurve':
      pursuit.anchors = [
        { x: -48, y: -48, ang: 0, out: 36, in: 36 },
        { x: 0, y: 0, ang: 72, out: 36, in: 36 },
        { x: 48, y: 48, ang: 0, out: 36, in: 36 }
      ];
      break;
  }
}

/** Replace the whole path with the 3-point arc (used by the field arc tool). */
export function setArcPath(p1: Pt, p2: Pt, p3: Pt) {
  pushHistory();
  pursuit.anchors = arcAnchors(p1, p2, p3);
  pursuit.selected = null;
}

/** Field editing tool: 'select' (default) or 'arc3' (picking three arc points). */
export const tool = $state<{ mode: 'select' | 'arc3'; pts: Pt[] }>({ mode: 'select', pts: [] });

export function startArcTool() {
  tool.mode = 'arc3';
  tool.pts = [];
}
export function cancelTool() {
  tool.mode = 'select';
  tool.pts = [];
}
/** Add a clicked point to the arc tool; commits the arc once three are placed. */
export function addArcPoint(p: Pt) {
  if (tool.mode !== 'arc3') return;
  tool.pts.push({ x: round(p.x), y: round(p.y) });
  if (tool.pts.length >= 3) {
    setArcPath(tool.pts[0], tool.pts[1], tool.pts[2]);
    cancelTool();
  }
}

// --- diagnostics (guardrails) --------------------------------------------------

/** Worst-case chord bulge (sagitta) between consecutive baked points, inches —
 *  the error introduced by the export spacing on the sharpest part of the curve. */
export function maxSagitta(dense: DenseSample[], spacing: number): number {
  let maxK = 0;
  for (const d of dense) {
    const k = Math.abs(d.k);
    if (k > maxK) maxK = k;
  }
  return (spacing * spacing * maxK) / 8;
}

function segCross(a: Pt, b: Pt, c: Pt, d: Pt): boolean {
  const o = (p: Pt, q: Pt, r: Pt) => Math.sign((q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x));
  return o(a, b, c) !== o(a, b, d) && o(c, d, a) !== o(c, d, b);
}

/** True if the baked polyline crosses itself (non-adjacent segments intersect). */
export function pathSelfIntersects(pts: Pt[]): boolean {
  const n = pts.length;
  for (let i = 0; i < n - 1; i++) {
    for (let j = i + 2; j < n - 1; j++) {
      if (i === 0 && j === n - 2) continue; // shared endpoints are fine
      if (segCross(pts[i], pts[i + 1], pts[j], pts[j + 1])) return true;
    }
  }
  return false;
}

// --- saved paths (folder-backed) ----------------------------------------------
// Saved paths are real .json files in the "trajectory paths" folder, served by
// the /api/paths dev endpoint — the same arrangement the sandbox has with
// "sandbox autons" (sandbox.svelte.ts), down to the flows below: the picker
// drawer is the folder listing, saving writes a file, and opening a file
// focuses its tab if one is already open rather than opening a second copy.

const PATHS_API = '/api/paths';

export interface SavedPath {
  name: string;
  savedAt: number;
  content: string; // JSON of { anchors, params }
}

/** Serialize the current path — the file's whole contents. */
function serializePath(): string {
  return JSON.stringify({ anchors: pursuit.anchors, params: pursuit.params });
}

/** Build a tab from a saved path's file text. Clean the moment it's created
 *  (it matches disk by definition) and with empty undo/redo: a freshly opened
 *  document has no prior state in THIS tab to undo into. Null when the file is
 *  corrupt or holds fewer than two usable anchors. */
function tabFromPathText(text: string, name: string): PathTabState | null {
  try {
    const d = JSON.parse(text) as { anchors?: unknown; params?: Partial<PursuitPlanParams> };
    const anchors = readAnchors(d.anchors);
    if (!anchors) return null;
    return tabFrom(name, anchors, { ...DEFAULT_PARAMS, ...(d.params ?? {}) });
  } catch {
    return null;
  }
}

/** Reload the saved-path menu from the folder. */
export async function refreshSavedPaths() {
  try {
    const res = await fetch(PATHS_API);
    if (res.ok) pursuit.saved = (await res.json()) as SavedPath[];
  } catch {
    // dev endpoint unavailable (e.g. static build) — leave the list as-is
  }
}

/** Write the current path to a file in the folder (name = file name). The
 *  low-level primitive both save flows write through — neither one prompts,
 *  so this is the only place that talks to the API. */
export async function savePath(name: string): Promise<void> {
  const trimmed = name.trim() || 'untitled';
  try {
    await fetch(PATHS_API, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ name: trimmed, content: serializePath() })
    });
  } catch {
    // ignore write failure
  }
  await refreshSavedPaths();
}

/** Save — writes back to the open document, no name prompt. A no-op when
 *  untitled; the Save button falls through to Save as in that case. */
export async function saveCurrentPath(): Promise<void> {
  const tab = pursuit.tabs[pursuit.activeTab];
  if (!tab?.name) return;
  await savePath(tab.name);
  markClean();
}

/** Save as — the only place a name is typed. Always renames the ACTIVE TAB to
 *  the new name, so "Save as" reliably means "this tab is now that file." */
export async function savePathAs(name: string): Promise<void> {
  const trimmed = name.trim() || 'untitled';
  await savePath(trimmed);
  const tab = pursuit.tabs[pursuit.activeTab];
  if (tab) tab.name = trimmed;
  markClean();
}

/** Open a saved path. Already open in a tab → focus it instead of opening a
 *  second copy; otherwise stash the current path, append a fresh tab loaded
 *  from the file, and focus that. Opening never loses anything, so unlike a
 *  close it needs no confirmation. */
export function loadPath(name: string): void {
  const existing = pursuit.tabs.findIndex((t) => t.name === name);
  if (existing !== -1) {
    switchTab(existing);
    return;
  }
  const file = pursuit.saved.find((s) => s.name === name);
  if (!file) return;
  const tab = tabFromPathText(file.content, name);
  if (!tab) return; // corrupt file — leave the workspace alone
  stashActiveTab();
  pursuit.tabs.push(tab);
  pursuit.activeTab = pursuit.tabs.length - 1;
  hydrateActiveTab();
  resetTransientUiState();
}

/** Delete a saved path's file. Any tab still open under that name keeps
 *  editing an in-memory copy — Save would recreate the file, same as any text
 *  editor after its file disappears out from under it. */
export async function deletePath(name: string): Promise<void> {
  try {
    await fetch(`${PATHS_API}?name=${encodeURIComponent(name)}`, { method: 'DELETE' });
  } catch {
    // ignore
  }
  await refreshSavedPaths();
}

// --- C++ export ---------------------------------------------------------------

/**
 * The paste-ready LucasLib snippet: the control points, and the call that
 * follows them.
 *
 * Deliberately NOT the baked rows. The brain bakes these same segments with
 * src/ramsete/util/profile.cpp \u2014 which is what the preview above just ran through
 * its port \u2014 so shipping geometry keeps one implementation of the profile
 * instead of two, and a path stays a few lines instead of a few hundred.
 *
 * Route-level knobs only appear when they differ from PathSettings's own
 * defaults, in which case the bake has to be spelled out; an untouched path is
 * the bare one-liner.
 */
export function exportCpp(): string {
  const plan = getPlan();
  const name = sanitize(pursuit.openPath ?? 'path'); // an untitled tab still needs a C++ identifier
  const segs = pathSegments(pursuit.anchors);
  if (!segs.length) return '// add a second anchor to make a path';

  const body = segs
    .map((s) => {
      const pt = (p: { x: number; y: number }) => `{${fmt(p.x)}, ${fmt(p.y)}}`;
      let line = `  {.a = ${pt(s.a)}, .b = ${pt(s.b)}, .c = ${pt(s.c)}, .d = ${pt(s.d)}`;
      if (s.maxVel !== undefined) line += `, .maxVel = ${fmt(s.maxVel)}`;
      if (s.stopEnd) line += `, .stopEnd = true`;
      return line + '}';
    })
    .join(',\n');

  // Only the fields that are no longer at their in-struct default.
  const p = pursuit.params;
  const cfg: string[] = [];
  if (p.maxVel !== DEFAULT_PARAMS.maxVel) cfg.push(`.maxVel = ${fmt(p.maxVel)}`);
  if (p.maxAccel !== DEFAULT_PARAMS.maxAccel) cfg.push(`.maxAccel = ${fmt(p.maxAccel)}`);
  if (p.velMargin !== DEFAULT_PARAMS.velMargin) cfg.push(`.velMargin = ${fmt(p.velMargin, 2)}`);
  if (p.endSpeed !== DEFAULT_PARAMS.endSpeed) cfg.push(`.endSpeed = ${fmt(p.endSpeed)}`);

  // The library takes the PathSettings by argument and bakes internally — there is
  // no longer an overload that accepts a pre-baked row list.
  const call = cfg.length
    ? `ramsete.trajectory(${name}, {${cfg.join(', ')}});`
    : `ramsete.trajectory(${name});`;

  const caps = segs.filter((s) => s.maxVel !== undefined).length;
  const capNote = caps ? ` \u00b7 ${caps} capped segment${caps > 1 ? 's' : ''}` : '';

  return `// ${segs.length} segment${segs.length > 1 ? 's' : ''} \u00b7 ${plan.total.toFixed(1)}" \u00b7 ~${plan.duration.toFixed(1)}s${capNote}
// Baked on the brain from these control points \u2014 the speed plan above is what
// createTrajectory() will produce from them.
std::vector<BezierSegment> ${name} = {
${body}
};
${call}`;
}

function sanitize(name: string): string {
  const s = name.trim().replace(/[^A-Za-z0-9_]/g, '_').replace(/^(\d)/, '_$1');
  return s || 'path';
}

function fmt(v: number, decimals = 1): string {
  let s = v.toFixed(decimals);
  if (s.includes('.')) s = s.replace(/0+$/, '').replace(/\.$/, '');
  return s || '0';
}
