// Sandbox mode state — route code text (the single source of truth), sim
// knobs (left panel), per-movement effort overrides, saved autos, selection +
// playback. Parse + simulation are memoized here so every component reads the
// same computed route.
//
// Feature 1 (editor tabs): the workspace is a list of independent buffers
// (sandbox.tabs), each with its own code/knobs/efforts, its own dirty
// signature, and its own undo/redo history (Feature 3). Only ONE tab is
// "live" at a time — the active tab's fields are mirrored out to
// sandbox.code/knobs/efforts, because that's what the rest of the app (the
// field, the params panel, getRoute()) reads and writes directly. Switching
// tabs stashes the outgoing buffer back into its own TabState, then hydrates
// the live fields from the incoming one (stashActiveTab/hydrateActiveTab).
// sandbox.openAuton and sandbox.dirty are kept in sync with the ACTIVE tab by
// a couple of small reactive effects rather than becoming real getters, so
// every existing call site that reads sandbox.openAuton/sandbox.dirty keeps
// working unchanged.

import {
  parseRoute,
  migrateCode,
  rewriteNumber,
  setStructField,
  fmtNum,
  type ParseResult,
  type StructInit,
  type PointLit,
  type PathDeclStmt,
  type CallStmt
} from '$lib/sandbox/parse';
import { simulate, DEFAULT_KNOBS, type SimKnobs, type SimResult, type Pose, type EffortOverrides } from '$lib/sandbox/sim';
import type { BezierSegment } from '$lib/sandbox/ramsete';
import { settings, holesToInches } from '$lib/stores/settings.svelte';
import { normalizeBackdropId, type FieldBackdrop } from '$lib/field/backdrops';

const STORAGE_KEY = 'lucaslib-debugger.sandbox';

// Field convention (ramsete.ts, SandboxField): 0 = +Y, clockwise positive —
// used below by the selected-anchor widget's DIR field and handle rotation.
const D2R = Math.PI / 180;
const R2D = 180 / Math.PI;

// Bezier paths (std::vector<BezierSegment>) live entirely in the route text —
// unlike the retired [TRAJECTORY] marker, there is no side-channel spline data
// for the store to keep. A control-point drag rewrites the PointLit literals
// in place via editPoint(), the same way any other numeric field does.

const DEFAULT_CODE = `// Sandbox route — real LucasLib calls, paste-ready for auton.cpp.
// Targets, headings and leads are draggable on the field; the text
// rewrites itself. Sim-only behavior lives in the left panel.
// NOTE: backward defaults to false — set 'backward = true;' to drive reversed.
// driveForward()/driveBackward() ignore it entirely — distance is always a
// positive magnitude, and which function you call is the only direction.

chassis.setPos(0, -62.5, 0);

driveForward(24, chassis.h, {.settleWithin = 1});
face(90);
moveTo(48, -24, {.t = 3000});

BoomParams goal {.x = 24, .y = 24, .heading = 315, .lead = 0.5, .gLead = 0.3};
facePoint(goal);
boomerangTo(goal, {.settleWithin = 2});

driveBackward(12);

// ramsete.trajectory follows a baked spline with the RAMSETE law instead of a
// point-to-point PID — the better tool for a long, curved run where chasing a
// single target would fight the path's own geometry. This one picks up where
// the drive above leaves off; the second segment's .a repeats the first
// segment's .d so the two stay joined (the parser rejects a gap over 0.05").
// .maxVel caps just its own segment, so splitting a curve is how you slow one
// stretch of it without touching the rest.
std::vector<BezierSegment> path = {
  {.a = {32, 16}, .b = {40, 30}, .c = {30, 48}, .d = {12, 54}},
  {.a = {12, 54}, .b = {-4, 60}, .c = {-20, 60}, .d = {-30, 54}, .maxVel = 24}
};
ramsete.trajectory(path);
`;

/** A saved auton = one .txt file in the "sandbox autons" folder. */
export interface SavedAuton {
  name: string; // = file name (without .txt)
  savedAt: number; // file mtime, ms epoch
  content: string; // full file text (params header + code)
}

/**
 * One editor tab (Feature 1) — its own buffer, its own "matches disk"
 * signature, its own undo/redo. Lucas flips between saved versions of one
 * route (sawp v1 … v4), so tabs must hold genuinely independent state, not
 * one shared buffer that just relabels itself; every field below is either
 * copied out of/into sandbox.code/knobs/efforts on switch (stashActiveTab /
 * hydrateActiveTab) or, for cleanSig/undo/redo, lives ONLY on the tab and
 * never gets mirrored anywhere.
 */
export interface TabState {
  id: string; // stable {#each} key, see makeId()
  name: string | null; // the saved file this tab is open as; null = untitled
  code: string;
  knobs: SimKnobs;
  efforts: EffortOverrides;
  /** the field backdrop THIS auton runs on. Deliberately per-tab and NOT
   *  settings.fieldBackdrop: a route is written against one game's field, so
   *  the backdrop belongs to the route the same way its knobs do. Settings'
   *  own picker still drives Replay and the Trajectory Generator; the sandbox
   *  simply doesn't read it. */
  field: FieldBackdrop;
  cleanSig: string; // this tab's own "matches disk" signature, see tabSignature()
  undo: string[]; // this tab's own undo stack (Feature 3) — snapshots of `code`
  redo: string[];
}

interface PersistedTab {
  name: string | null;
  code: string;
  knobs: Record<string, number | boolean>;
  efforts: EffortOverrides;
  /** absent in blobs written before the per-auton field existed — those tabs
   *  inherit settings.fieldBackdrop on load, so what the sandbox was already
   *  showing is what it keeps showing. */
  field?: FieldBackdrop;
  /** the tab's own cleanSig at its last save/load — persisted VERBATIM
   *  (never recomputed from `code`) so a reload can tell a tab that still
   *  has unsaved changes from one that matches disk. Recomputing it from the
   *  persisted code instead would silently launder every tab clean on every
   *  reload, which defeats the whole point of the unsaved dot. */
  cleanSig: string;
}

interface Persisted {
  tabs: PersistedTab[];
  activeTab: number;
  /** bumped when a knob's MEANING changes, so stale values get re-defaulted —
   *  shared across every tab, since they were all written by the same app
   *  version's understanding of what each knob means. */
  knobsVersion?: number;
}

// 2 = the lib's CROSS rewrite: crossR is gone and crossD flipped from "exit within
//     this distance of the ⊥ line" to "which side of it", where 0 is ARMED (line on
//     the target) and negative is off. A stored crossD of 0 meant OFF under v1, so
//     carrying it over would silently arm cross on every move.
// 3 = turns gained setR/setD settle, and extA changed from "the settle activation
//     zone" to a separate UNGATED exit. A stored extA of 10 would now hard-exit
//     every turn 10° short without waiting for it to stop.
// 4: exit conditions became t / v / d / settleWithin with a speed-based settle.
//    (The field was called settleGate for a few days; the lib renamed it to
//    settleWithin, and the knob keys renamed with it — stale ecLatGate /
//    ecTurnGate entries simply no longer match a key and fall back to default.)
// 5: ecLatV / ecTurnV's own DEFAULTS changed (exitConditions.h: lateral 10 -> 2
//    in/s, turn 10 -> 0.35 rad/s) — the old values were far too fast to ever
//    gate a real settle (turn especially: 10 rad/s = 573 deg/s, unreachable). A
//    stale persisted 10 would silently keep the broken early-exit behavior.
// 6: robotWeight changed unit — pounds (2-10) to kilograms (1-20). A stale
//    pounds value clamped into the new range would silently be reinterpreted
//    as that many kilograms (e.g. a stored 6 lb becoming 6 kg), so it resets
//    to the default alongside the EC knobs below instead of just clamping.
// 7: laterals gained the no-overshoot brake envelope that turns already had
//    (lateralPowMag in sim.ts), which changes what driveKp DOES: it can no
//    longer overshoot, so it only sets how hard the robot pushes under the
//    curve. A gain tuned for the old P-only law (0.45) leaves the movement
//    crawling in, so it resets to the new 2 V/in default.
const KNOBS_VERSION = 7;

/**
 * Reset the knobs whose MEANING changed with a version bump — a stale value is
 * worse than no value once the number means something different. Shared by
 * load() (localStorage) and parseRouteFile() (a saved auton file), because
 * both restore knobs written under an older version.
 */
function migrateKnobs(knobs: SimKnobs) {
  knobs.robotWeight = DEFAULT_KNOBS.robotWeight;
  knobs.driveKp = DEFAULT_KNOBS.driveKp;
  knobs.ecLatT = DEFAULT_KNOBS.ecLatT;
  knobs.ecLatV = DEFAULT_KNOBS.ecLatV;
  knobs.ecLatD = DEFAULT_KNOBS.ecLatD;
  knobs.ecLatWithin = DEFAULT_KNOBS.ecLatWithin;
  knobs.ecTurnT = DEFAULT_KNOBS.ecTurnT;
  knobs.ecTurnV = DEFAULT_KNOBS.ecTurnV;
  knobs.ecTurnD = DEFAULT_KNOBS.ecTurnD;
  knobs.ecTurnWithin = DEFAULT_KNOBS.ecTurnWithin;
}

/** Pull same-version values that are corrupt or hand-edited back into range. */
function clampKnobs(knobs: SimKnobs) {
  knobs.robotWeight = Math.min(20, Math.max(1, knobs.robotWeight));
}

// --- tabs (Feature 1) -----------------------------------------------------------

function makeId(): string {
  return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
}

/**
 * A tab's "does this match what was last saved/loaded" fingerprint — the
 * per-tab replacement for the old module-level routeSignature/cleanSignature
 * pair (see sandbox.dirty's own doc below for why dirty tracking is its own
 * effect). trackwidth stays excluded for the same reason it always was: it's
 * a derived pass-through of the global robot width (overwritten every render
 * regardless of which tab is open), not something any one document owns —
 * closing over it here would mark every tab dirty the moment Settings'
 * robot width changes.
 */
function tabSignature(code: string, knobs: SimKnobs, efforts: EffortOverrides, field: FieldBackdrop): string {
  const { trackwidth, ...restKnobs } = knobs;
  void trackwidth;
  // `field` is in here because serializeRoute writes it into the file header —
  // anything that round-trips to disk has to be able to make a tab dirty, or
  // switching an auton's field would leave the .txt silently stale.
  return code + '␟' + JSON.stringify(restKnobs) + '␟' + JSON.stringify(efforts) + '␟' + field;
}

/** A brand-new untitled tab, starter code + default knobs — clean the moment
 *  it's created, empty undo/redo (nothing to undo into yet). Takes the field
 *  from whoever is opening it (normally the tab you were just on), since a
 *  new auton for the same game wants the same backdrop far more often than it
 *  wants an empty one. */
function makeTab(field: FieldBackdrop): TabState {
  const code = DEFAULT_CODE;
  const knobs: SimKnobs = { ...DEFAULT_KNOBS };
  const efforts: EffortOverrides = {};
  return {
    id: makeId(),
    name: null,
    code,
    knobs,
    efforts,
    field,
    cleanSig: tabSignature(code, knobs, efforts, field),
    undo: [],
    redo: []
  };
}

function load(): { tabs: TabState[]; activeTab: number } {
  if (typeof localStorage !== 'undefined') {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (raw) {
        const p = JSON.parse(raw) as Partial<Persisted>;
        if (Array.isArray(p.tabs) && p.tabs.length) {
          const tabs = p.tabs.map((pt): TabState => {
            const knobs: SimKnobs = { ...DEFAULT_KNOBS };
            const rawKnobs = pt?.knobs as Record<string, number | boolean> | undefined;
            for (const k of Object.keys(DEFAULT_KNOBS) as (keyof SimKnobs)[]) {
              const v = rawKnobs?.[k];
              // knobs are numbers or booleans (omniWheels) — type-match the default
              if (typeof v === typeof DEFAULT_KNOBS[k]) (knobs as unknown as Record<string, number | boolean>)[k] = v!;
            }
            if (p.knobsVersion !== KNOBS_VERSION) migrateKnobs(knobs);
            clampKnobs(knobs);
            const code = typeof pt?.code === 'string' && pt.code.trim() ? migrateCode(pt.code) : DEFAULT_CODE;
            const efforts = pt?.efforts ?? {};
            // No field key = written before this was per-auton, so inherit the
            // global one the sandbox used to read (see PersistedTab.field).
            const field = pt?.field ? normalizeBackdropId(pt.field) : settings.fieldBackdrop;
            return {
              id: makeId(),
              name: typeof pt?.name === 'string' ? pt.name : null,
              code,
              knobs,
              efforts,
              field,
              // Restore the tab's own checkpoint verbatim so a tab with
              // unsaved changes reloads still showing its dot (see
              // PersistedTab.cleanSig's own doc) — a blob written before this
              // field existed has none, and the safe fallback there is to
              // treat it as clean rather than fabricate a mismatch.
              cleanSig: typeof pt?.cleanSig === 'string' ? pt.cleanSig : tabSignature(code, knobs, efforts, field),
              undo: [],
              redo: []
            };
          });
          const activeTab = typeof p.activeTab === 'number' ? Math.min(Math.max(0, p.activeTab), tabs.length - 1) : 0;
          return { tabs, activeTab };
        }
      }
    } catch {
      // corrupt — reset
    }
  }
  return { tabs: [makeTab(settings.fieldBackdrop)], activeTab: 0 };
}

const initial = load();

export const sandbox = $state<{
  tabs: TabState[];
  activeTab: number;
  code: string;
  knobs: SimKnobs;
  efforts: EffortOverrides;
  /** the ACTIVE tab's field backdrop, mirrored out of tabs[activeTab].field
   *  exactly like code/knobs/efforts. SandboxField reads this instead of
   *  settings.fieldBackdrop; write to it to change the open auton's field. */
  field: FieldBackdrop;
  saved: SavedAuton[];
  /** selected statement index (into parse result stmts), or null */
  selected: number | null;
  /** which segment of the ACTIVE path a control-point click last landed on —
   *  targets Insert midpoint / Delete anchor when the panel has no other way
   *  to know which joint the user means. Cleared whenever `selected` changes. */
  selectedSeg: number | null;
  /** which ANCHOR of the active path's control-point model is selected — the
   *  sandbox's BezierSegment[] equivalent of the Trajectory Generator's own
   *  `pursuit.selected` anchor index (PursuitParams.svelte). Drives the
   *  selected-anchor widget in SandboxPathPanel. Same lifecycle as
   *  selectedSeg: cleared whenever `selected` changes — which is also what
   *  "the path panel closes" means here, since +page.svelte's
   *  `pathPanelActive` derives straight from `selected`. */
  selectedAnchor: number | null;
  /** the ACTIVE tab's file name, or null for untitled (Feature 1) — kept in
   *  sync with tabs[activeTab].name by a reactive effect below rather than
   *  becoming a real getter, so every existing call site that reads
   *  sandbox.openAuton keeps working unchanged. */
  openAuton: string | null;
  /** true once the ACTIVE tab's code/knobs/efforts have changed since its
   *  last load or save. Drives the header's unsaved dot and Save's disabled
   *  state; every OTHER tab's own dot comes from isTabDirty() instead. */
  dirty: boolean;
  time: number; // playhead ms over the simulated route
  playing: boolean;
}>({
  tabs: initial.tabs,
  activeTab: initial.activeTab,
  code: initial.tabs[initial.activeTab].code,
  knobs: { ...initial.tabs[initial.activeTab].knobs },
  efforts: { ...initial.tabs[initial.activeTab].efforts },
  field: initial.tabs[initial.activeTab].field,
  saved: [], // populated from the "sandbox autons" folder via refreshSavedAutons()
  selected: null,
  selectedSeg: null,
  selectedAnchor: null,
  openAuton: initial.tabs[initial.activeTab].name,
  dirty: false,
  time: 0,
  playing: false
});

// selectedSeg only means something for the CURRENTLY selected statement — a
// stale index from a different path is worse than none.
let lastSelectedStmt: number | null = null;
$effect.root(() => {
  $effect(() => {
    if (sandbox.selected !== lastSelectedStmt) {
      lastSelectedStmt = sandbox.selected;
      sandbox.selectedSeg = null;
      sandbox.selectedAnchor = null;
    }
  });
});

// Trackwidth is NOT a knob: it tracks the robot-settings width minus 3 in
// (wheel centers sit inboard of the chassis edge).
$effect.root(() => {
  $effect(() => {
    sandbox.knobs.trackwidth = Math.max(6, holesToInches(settings.robotWidthHoles) - 3);
  });
  // Persist the whole workspace (Feature 1): every tab's own buffer, plus
  // which one is focused. Saved autons live as files in the folder, not in
  // localStorage — this is just enough for a reload to restore the tab strip
  // as it was.
  //
  // This effect only READS $state (sandbox.tabs, sandbox.activeTab, and —
  // for the active slot specifically — the live-mirrored sandbox.code/knobs/
  // efforts) and writes to localStorage, never back into sandbox.tabs. An
  // earlier version of this effect also wrote tabs[activeTab] through from
  // the live buffer so OTHER code could read it continuously; nothing
  // actually needs that (isTabDirty reads sandbox.dirty for the active tab,
  // not tabs[activeTab].code — see its own doc), and a reactive effect that
  // both reads and writes state derived from the same source risks exactly
  // the "is this effect updating something it depends on" loop Svelte's
  // dev-mode diagnostics flag. stashActiveTab() below still does the
  // equivalent write explicitly, but only at the one moment it's actually
  // needed: right before switching away from a tab.
  $effect(() => {
    if (typeof localStorage === 'undefined') return;
    const activeTab = sandbox.activeTab;
    const data: Persisted = {
      tabs: sandbox.tabs.map((t, i) =>
        i === activeTab
          ? {
              name: t.name,
              code: sandbox.code,
              knobs: { ...sandbox.knobs },
              efforts: { ...sandbox.efforts },
              field: sandbox.field,
              cleanSig: t.cleanSig
            }
          : { name: t.name, code: t.code, knobs: { ...t.knobs }, efforts: { ...t.efforts }, field: t.field, cleanSig: t.cleanSig }
      ),
      activeTab,
      knobsVersion: KNOBS_VERSION
    };
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(data));
    } catch {
      // quota — ignore
    }
  });
});

// --- dirty tracking (per-tab, Feature 1) -------------------------------------
// "Dirty" = the ACTIVE tab's code/knobs/efforts differ from the signature
// taken at its last load or save (TabState.cleanSig). Deliberately its own
// effect rather than folded into the persist effect above — that effect
// fires on every keystroke too (it has to, to keep localStorage current), so
// if it also set `dirty = true` unconditionally, LOADING a file would flip
// dirty on merely by writing sandbox.code/knobs/efforts, which is exactly
// backwards. Comparing against the tab's own cleanSig instead means
// loading/saving can clear it by updating that signature, and nothing else
// can set it by accident.
/** Mark the ACTIVE tab's buffer as matching what's on disk (or the starter
 *  code) — called by the save flows (a freshly created/opened tab is already
 *  clean the moment makeTab()/tabFromRouteText() builds its cleanSig). */
function markClean() {
  const tab = sandbox.tabs[sandbox.activeTab];
  if (tab) tab.cleanSig = tabSignature(sandbox.code, sandbox.knobs, sandbox.efforts, sandbox.field);
  sandbox.dirty = false;
}
$effect.root(() => {
  $effect(() => {
    const tab = sandbox.tabs[sandbox.activeTab];
    sandbox.dirty = tab ? tabSignature(sandbox.code, sandbox.knobs, sandbox.efforts, sandbox.field) !== tab.cleanSig : false;
  });
  // sandbox.openAuton mirrors tabs[activeTab].name — see the field's own doc
  // above for why this stays a plain synced field instead of a getter.
  $effect(() => {
    sandbox.openAuton = sandbox.tabs[sandbox.activeTab]?.name ?? null;
  });
});

/** Whether tab `i` differs from its own last-saved/loaded signature — drives
 *  each tab's unsaved dot in the strip. The active tab reads the live
 *  mirrored buffer (its own tabs[i].code is stale until the persist effect's
 *  write-through catches up); every other tab reads its own stored fields
 *  directly, since nothing else keeps them live. */
export function isTabDirty(i: number): boolean {
  const t = sandbox.tabs[i];
  if (!t) return false;
  if (i === sandbox.activeTab) return sandbox.dirty;
  return tabSignature(t.code, t.knobs, t.efforts, t.field) !== t.cleanSig;
}

// --- undo / redo (Feature 3) --------------------------------------------------
// Mirrors pursuit.svelte.ts's pushHistory/undo/redo in shape, but the
// snapshot is just the route TEXT (sandbox.code) — knobs/efforts aren't part
// of what ⌘Z restores. History lives ON the tab (TabState.undo/redo) rather
// than in a module-level stack, so switching tabs switches history with the
// buffer: undo can never reach across tabs, because there is no shared stack
// for it to reach through.

const HIST_CAP = 120; // matches pursuit's cap
const COALESCE_MS = 700; // matches pursuit's COALESCE_MS

let lastUndoKey = '';
let lastUndoAt = 0;

/**
 * Record sandbox.code onto the ACTIVE tab's undo stack before a change. Pass
 * a `coalesceKey` for continuous edits (typing, a slider drag) so a burst of
 * same-key pushes within {@link COALESCE_MS} collapses into one undo step —
 * the same contract as pursuit's pushHistory. A drag on the field instead
 * pushes with NO key, gated by a `dragDirty` flag local to the drag handler
 * (SandboxField.svelte) so it fires exactly once at gesture start — a slow,
 * deliberate drag can easily run past the coalescing window, where relying on
 * time alone would incorrectly split one drag into several undo steps.
 */
export function pushUndo(coalesceKey = ''): void {
  const tab = sandbox.tabs[sandbox.activeTab];
  if (!tab) return;
  const now = Date.now();
  if (coalesceKey && coalesceKey === lastUndoKey && now - lastUndoAt < COALESCE_MS && tab.undo.length) {
    lastUndoAt = now;
    tab.redo = [];
    return;
  }
  tab.undo.push(sandbox.code);
  if (tab.undo.length > HIST_CAP) tab.undo.shift();
  tab.redo = [];
  lastUndoKey = coalesceKey;
  lastUndoAt = now;
}

/** Undo the active tab's last recorded change. Caret placement after an
 *  undo is the caller's job (SandboxCode.svelte calls SandboxEditor's
 *  syncExternalChange) — this function only owns the text. */
export function undo(): void {
  const tab = sandbox.tabs[sandbox.activeTab];
  if (!tab || !tab.undo.length) return;
  tab.redo.push(sandbox.code);
  if (tab.redo.length > HIST_CAP) tab.redo.shift();
  sandbox.code = tab.undo.pop()!;
  lastUndoKey = ''; // the next push starts a fresh coalescing window
}

export function redo(): void {
  const tab = sandbox.tabs[sandbox.activeTab];
  if (!tab || !tab.redo.length) return;
  tab.undo.push(sandbox.code);
  if (tab.undo.length > HIST_CAP) tab.undo.shift();
  sandbox.code = tab.redo.pop()!;
  lastUndoKey = '';
}

// --- memoized parse + sim ---------------------------------------------------

export interface Route {
  parsed: ParseResult;
  sim: SimResult;
}

let cacheKey = '';
let cacheVal: Route | null = null;

/** Parse + simulate the current code (memoized). Reactive: reads code+knobs+efforts. */
export function getRoute(): Route {
  const key = sandbox.code + '␟' + JSON.stringify(sandbox.knobs) + '␟' + JSON.stringify(sandbox.efforts);
  if (key !== cacheKey || !cacheVal) {
    const parsed = parseRoute(sandbox.code);
    const sim = simulate(parsed, { ...sandbox.knobs }, { ...sandbox.efforts });
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
//
// editNumber/editStructField/editPoint deliberately push NO undo history of
// their own — they're the primitives a per-frame field drag calls many times
// a second (SandboxField.svelte), and pushing here would record one snapshot
// per frame instead of one per gesture. The caller pushes once, at drag
// start, via a dragDirty flag (mirrors pursuit's own moveAnchor/moveHandle,
// which are equally push-free for the same reason).

export function editNumber(lit: { start: number; end: number }, value: number, decimals = 1) {
  sandbox.code = rewriteNumber(sandbox.code, lit, value, decimals);
}

export function editStructField(init: StructInit, name: string, value: number, decimals = 1) {
  sandbox.code = setStructField(sandbox.code, init, name, value, decimals);
}

/** Move one BezierSegment control point; rewrites both literals in one pass. */
export function editPoint(pt: PointLit, x: number, y: number, decimals = 1) {
  // Rewrite y first — it sits AFTER x in the source (`{x, y}`) — so x's
  // [start,end) range is still valid for the second rewrite.
  let code = rewriteNumber(sandbox.code, pt.y, y, decimals);
  code = rewriteNumber(code, pt.x, x, decimals);
  sandbox.code = code;
}

// --- path panel: structural edits (add / insert / delete / mirror / reverse) --
// Point drags rewrite one PointLit literal at a time through editPoint() above.
// The operations below change the NUMBER of segments (or remap every point at
// once), so they regenerate the whole declaration body instead — see
// rewritePath(). Every one of them reads the CURRENT decl fresh via
// segsFromPathDecl(), so stale ranges from a previous edit never leak in.

/** Plain-object form of one BezierSegment — what the path panel's structural
 *  ops operate on before rewritePath() serializes them back into route text.
 *  Shape-identical to ramsete.ts's BezierSegment; named separately here
 *  because these are throwaway working copies, not the library's own type. */
export type SegModel = BezierSegment;

/** BezierSegment's own in-struct scalar defaults — an emitted field is only
 *  written when it differs from these (mirrors BEZIER_SEG in spec.ts). */
const SEG_DEFAULTS = { reversed: false, stopEnd: false, maxVel: -1, maxAccel: -1 };

/** A parsed path declaration's segments as plain BezierSegment objects. */
export function segsFromPathDecl(decl: PathDeclStmt): SegModel[] {
  return decl.segs.map((seg) => {
    const pt = (k: 'a' | 'b' | 'c' | 'd') => ({
      x: seg.points[k]?.x.value ?? 0,
      y: seg.points[k]?.y.value ?? 0
    });
    return {
      a: pt('a'),
      b: pt('b'),
      c: pt('c'),
      d: pt('d'),
      reversed: (seg.fields.reversed?.value as boolean | undefined) ?? SEG_DEFAULTS.reversed,
      stopEnd: (seg.fields.stopEnd?.value as boolean | undefined) ?? SEG_DEFAULTS.stopEnd,
      maxVel: (seg.fields.maxVel?.value as number | undefined) ?? SEG_DEFAULTS.maxVel,
      maxAccel: (seg.fields.maxAccel?.value as number | undefined) ?? SEG_DEFAULTS.maxAccel
    };
  });
}

function fmtSeg(seg: SegModel): string {
  const pt = (p: { x: number; y: number }) => `{${fmtNum(p.x)}, ${fmtNum(p.y)}}`;
  let s = `{.a = ${pt(seg.a)}, .b = ${pt(seg.b)}, .c = ${pt(seg.c)}, .d = ${pt(seg.d)}`;
  if ((seg.reversed ?? false) !== SEG_DEFAULTS.reversed) s += `, .reversed = ${seg.reversed}`;
  if ((seg.stopEnd ?? false) !== SEG_DEFAULTS.stopEnd) s += `, .stopEnd = ${seg.stopEnd}`;
  if ((seg.maxVel ?? -1) !== SEG_DEFAULTS.maxVel) s += `, .maxVel = ${fmtNum(seg.maxVel as number)}`;
  if ((seg.maxAccel ?? -1) !== SEG_DEFAULTS.maxAccel) s += `, .maxAccel = ${fmtNum(seg.maxAccel as number)}`;
  return s + '}';
}

/** Rewrite a path declaration's segment list, preserving the surrounding
 *  text. The single choke point every structural path op (pathAddAnchor,
 *  pathInsertMidpoint, pathInsertAt, pathDeleteAnchor, pathMirror,
 *  pathReverse, and pathExtendTo/pathAddAnchor via pathExtend) writes through
 *  exactly once per call — so pushing undo here, rather than in each of
 *  them, covers all of them in one place (Feature 3). */
export function rewritePath(decl: PathDeclStmt, segs: SegModel[]): void {
  pushUndo();
  const body = segs.map((s) => '  ' + fmtSeg(s)).join(',\n');
  const text = `std::vector<BezierSegment> ${decl.name} = {\n${body}\n};`;
  sandbox.code = sandbox.code.slice(0, decl.start) + text + sandbox.code.slice(decl.end);
}

/** The path declaration the CURRENT selection is editing: either the
 *  pathDecl statement itself, or the std::vector<BezierSegment> an
 *  trajectory call on the selected line references. Mirrors
 *  SandboxField.svelte's own `activePath` derivation. */
export function activePathDecl(): PathDeclStmt | null {
  const { parsed } = getRoute();
  const idx = sandbox.selected;
  if (idx === null) return null;
  const s = parsed.stmts[idx];
  if (!s) return null;
  if (s.kind === 'pathDecl') return s;
  if (s.kind === 'call' && s.fn === 'ramsete.trajectory') {
    const a0 = s.args[0];
    if (a0?.kind === 'ident') return parsed.paths[a0.name] ?? null;
  }
  return null;
}

// --- selected-anchor widget (Feature 1, pre-existing) -------------------------
// The Trajectory Generator's per-anchor editor (PursuitParams.svelte) ported
// onto a sandbox path, which has no anchor model of its own — see the module
// doc up top on mapping anchor i onto BezierSegment. Every helper below
// re-reads the declaration by NAME (freshPath) rather than threading a decl
// object through several edits, because editPoint rewrites sandbox.code and
// shifts every later literal's [start,end) range — the same reason
// SandboxField's onPathDragMove re-derives `decl` between writes.
//
// moveAnchor stays push-free like editPoint/editNumber/editStructField above
// — it's shared with SandboxField's control-point drag (onPathDragMove),
// which needs the dragDirty single-push-per-gesture treatment, so
// SandboxPathPanel.svelte pushes explicitly before calling it for its own
// (discrete) X/Y tiles instead. setAnchorDir/setAnchorHandle/setAnchorMaxVel
// are never called from a drag, only from discrete widget controls, so each
// pushes internally with its own coalesce key — the same shape as pursuit's
// editAnchor/setSegmentCap.

/** Re-read a path declaration after a text edit — freshPathDecl in
 *  SandboxField does the same thing keyed by caret position; this is keyed
 *  by name because the anchor widget already has the path's name in hand. */
function freshPath(name: string): PathDeclStmt | undefined {
  return getRoute().parsed.paths[name];
}

/** Anchor i's own point literal: seg[i].a for every anchor except the last,
 *  which is seg[n-1].d — the two ranges that must move together at a joint. */
function anchorPointLit(decl: PathDeclStmt, i: number): PointLit | undefined {
  const segs = decl.segs;
  if (i < 0 || i > segs.length) return undefined;
  return i < segs.length ? segs[i].points.a : segs[segs.length - 1]?.points.d;
}
/** Anchor i's outgoing handle (seg[i].b) — absent on the final anchor. */
function anchorOutHandleLit(decl: PathDeclStmt, i: number): PointLit | undefined {
  return decl.segs[i]?.points.b;
}
/** Anchor i's incoming handle (seg[i-1].c) — absent on the first anchor. */
function anchorInHandleLit(decl: PathDeclStmt, i: number): PointLit | undefined {
  return decl.segs[i - 1]?.points.c;
}
/** Unit tangent through anchor i, from whichever handle exists (they're kept
 *  collinear, so either one gives the same direction). Falls back to +Y only
 *  when both handles have collapsed onto the anchor itself. */
function anchorDirVector(decl: PathDeclStmt, i: number): { x: number; y: number } {
  const pt = anchorPointLit(decl, i);
  const outH = anchorOutHandleLit(decl, i);
  const inH = anchorInHandleLit(decl, i);
  if (pt && outH) {
    const len = Math.hypot(outH.x.value - pt.x.value, outH.y.value - pt.y.value);
    if (len > 1e-6) return { x: (outH.x.value - pt.x.value) / len, y: (outH.y.value - pt.y.value) / len };
  }
  if (pt && inH) {
    const len = Math.hypot(pt.x.value - inH.x.value, pt.y.value - inH.y.value);
    if (len > 1e-6) return { x: (pt.x.value - inH.x.value) / len, y: (pt.y.value - inH.y.value) / len };
  }
  return { x: 0, y: 1 };
}
/** Global anchor index i -> which segment/key holds its own point literal. */
function anchorSegKey(decl: PathDeclStmt, i: number): { segIndex: number; key: 'a' | 'd' } | null {
  const n = decl.segs.length;
  if (i < 0 || i > n) return null;
  return i < n ? { segIndex: i, key: 'a' } : { segIndex: n - 1, key: 'd' };
}

export interface AnchorInfo {
  index: number;
  x: number;
  y: number;
  dir: number; // deg, field convention: 0 = +Y, clockwise positive
  handleOut: number | null; // outgoing handle length, in — absent on the final anchor
  handleIn: number | null; // incoming handle length, in — absent on the first anchor
  isFirst: boolean;
  isLast: boolean;
  isJoint: boolean; // an interior anchor — the only kind pathDeleteAnchor can remove
  maxVel: number; // .maxVel on the segment this anchor STARTS, -1 = profile default
}

/**
 * Read-only snapshot of anchor `i` of `decl`, for the widget to render — the
 * BezierSegment equivalent of reading `pursuit.anchors[i]` directly, since a
 * sandbox path keeps no such object and has to derive one from the current
 * parse instead. DIR and the two handle lengths follow the mapping in the
 * module doc exactly. Returns null once `i` no longer names an anchor (a
 * structural edit removed it out from under the selection).
 */
export function anchorInfo(decl: PathDeclStmt, i: number): AnchorInfo | null {
  const n = decl.segs.length;
  const pt = anchorPointLit(decl, i);
  if (!pt) return null;
  const outH = anchorOutHandleLit(decl, i);
  const inH = anchorInHandleLit(decl, i);
  const ax = pt.x.value;
  const ay = pt.y.value;

  // DIR: atan2(b - a) for any anchor with an outgoing handle, atan2(d - c)
  // for the final one (no outgoing handle to read there).
  let dir = 0;
  if (outH) dir = Math.atan2(outH.x.value - ax, outH.y.value - ay) * R2D;
  else if (inH) dir = Math.atan2(ax - inH.x.value, ay - inH.y.value) * R2D;
  if (dir < 0) dir += 360;

  const segIdx = Math.min(i, Math.max(0, n - 1));
  const seg = decl.segs[segIdx];
  const maxVel = (seg?.fields.maxVel?.value as number | undefined) ?? -1;

  return {
    index: i,
    x: ax,
    y: ay,
    dir,
    handleOut: outH ? Math.hypot(outH.x.value - ax, outH.y.value - ay) : null,
    handleIn: inH ? Math.hypot(ax - inH.x.value, ay - inH.y.value) : null,
    isFirst: i === 0,
    isLast: i === n,
    isJoint: i > 0 && i < n,
    maxVel
  };
}

/**
 * Move anchor `i` of path `pathName` to (x, y) — the joint-translation logic
 * behind both a field drag and the widget's X/Y tiles, factored out here so
 * there is exactly one implementation of "moving a joint moves both
 * coincident points and slides their own handles with it" (SandboxField's
 * onPathDragMove calls this too, for its own 'a'/'d' drags, instead of
 * duplicating it). At an interior anchor, seg[i-1].d and seg[i].a are the
 * SAME physical point (the parser rejects a gap over 0.05"), so the partner
 * copy moves too, and both adjacent handles translate by the same delta so
 * the curve slides through the joint instead of kinking.
 *
 * Push-free (see the section doc above) — SandboxField's drag and
 * SandboxPathPanel's typed X/Y tiles push their own undo step before calling
 * this, exactly like pursuit's own moveAnchor.
 */
export function moveAnchor(pathName: string, i: number, x: number, y: number): void {
  const decl0 = freshPath(pathName);
  const sk = decl0 && anchorSegKey(decl0, i);
  const pt0 = sk && decl0!.segs[sk.segIndex].points[sk.key];
  if (!sk || !pt0) return;
  const dx = x - pt0.x.value;
  const dy = y - pt0.y.value;
  editPoint(pt0, x, y);

  const { segIndex, key } = sk;
  const partnerIndex = key === 'd' ? segIndex + 1 : segIndex - 1;
  const partnerKey: 'a' | 'd' = key === 'd' ? 'a' : 'd';
  const isJoint = partnerIndex >= 0 && partnerIndex < decl0!.segs.length;

  if (isJoint) {
    const partnerPt = freshPath(pathName)?.segs[partnerIndex]?.points[partnerKey];
    if (partnerPt) editPoint(partnerPt, x, y);
  }

  const ownHandle = freshPath(pathName)?.segs[segIndex]?.points[key === 'd' ? 'c' : 'b'];
  if (ownHandle) editPoint(ownHandle, ownHandle.x.value + dx, ownHandle.y.value + dy);

  if (isJoint) {
    const partnerHandle = freshPath(pathName)?.segs[partnerIndex]?.points[partnerKey === 'a' ? 'b' : 'c'];
    if (partnerHandle) editPoint(partnerHandle, partnerHandle.x.value + dx, partnerHandle.y.value + dy);
  }
}

/** Rotate both of an anchor's handles to lie along `degrees`, keeping each
 *  one's own length — the collinearity a handle-based spline needs for C1
 *  continuity (bezierTools.h), the same invariant SandboxField's handle-drag
 *  mirroring keeps one drag at a time. A handle that doesn't exist (the
 *  first/last anchor) is simply left alone. Only ever called from the
 *  widget's Dir tile (a discrete commit-on-blur, not a drag), so it pushes
 *  its own undo step. */
export function setAnchorDir(pathName: string, i: number, degrees: number): void {
  pushUndo('anchorDir:' + pathName + ':' + i);
  const decl0 = freshPath(pathName);
  if (!decl0) return;
  const pt = anchorPointLit(decl0, i);
  if (!pt) return;
  const ax = pt.x.value;
  const ay = pt.y.value;
  const rad = degrees * D2R;
  const ux = Math.sin(rad);
  const uy = Math.cos(rad);

  const outH = anchorOutHandleLit(decl0, i);
  if (outH) {
    const len = Math.hypot(outH.x.value - ax, outH.y.value - ay);
    editPoint(outH, ax + ux * len, ay + uy * len);
  }
  const decl1 = freshPath(pathName) ?? decl0;
  const inH = anchorInHandleLit(decl1, i);
  if (inH) {
    const len = Math.hypot(ax - inH.x.value, ay - inH.y.value);
    editPoint(inH, ax - ux * len, ay - uy * len);
  }
}

/** Set one handle's length along the anchor's current tangent, with no side
 *  effects on the other handle. */
function setHandleLenRaw(pathName: string, i: number, side: 'in' | 'out', length: number): void {
  const decl = freshPath(pathName);
  if (!decl) return;
  const pt = anchorPointLit(decl, i);
  const h = side === 'out' ? anchorOutHandleLit(decl, i) : anchorInHandleLit(decl, i);
  if (!pt || !h) return;
  const u = anchorDirVector(decl, i);
  const sign = side === 'out' ? 1 : -1;
  editPoint(h, pt.x.value + sign * u.x * length, pt.y.value + sign * u.y * length);
}

/** Handle in/out sliders. 3" floor matches pursuit.svelte.ts's editAnchor —
 *  short enough to read as an intentional sharp corner, long enough to stay
 *  a real tangent. Only ever slider-driven from the widget (never a field
 *  drag), so it pushes its own undo step with a coalesce key — a continuous
 *  slider drag collapses into one step the same way pursuit's editAnchor
 *  does. */
export function setAnchorHandle(pathName: string, i: number, side: 'in' | 'out', length: number): void {
  pushUndo('anchorHandle:' + pathName + ':' + i + ':' + side);
  setHandleLenRaw(pathName, i, side, Math.max(3, length));
}

/**
 * Limit speed here — writes the real `.maxVel` field on the segment this
 * anchor STARTS (seg[i], or seg[n-1] for the final anchor): there is no
 * per-anchor speed in the library, only bakeTrajectory's per-segment ceiling
 * (speedCeiling, ramsete.ts). `value === null` clears it back to -1, the
 * same "profile default" sentinel every other -1 field on BezierSegment
 * uses (spec.ts's BEZIER_SEG) — parse.ts has no field-removal helper, so
 * this writes the sentinel explicitly rather than deleting the field; the
 * two read identically everywhere else this file already does the same.
 */
export function setAnchorMaxVel(pathName: string, i: number, value: number | null): void {
  pushUndo('anchorMaxVel:' + pathName + ':' + i);
  const decl = freshPath(pathName);
  if (!decl) return;
  const segIdx = Math.min(i, decl.segs.length - 1);
  const seg = decl.segs[segIdx];
  if (!seg) return;
  editStructField(seg, 'maxVel', value === null ? -1 : Math.max(0, value), 1);
}

/** The direction the path is travelling as it leaves its last segment. Falls
 *  back to the chord when the handle has collapsed onto the endpoint, and to
 *  +Y when even that is degenerate, so an extension always has a direction. */
function exitTangent(last: SegModel): { x: number; y: number } {
  let ux = last.d.x - last.c.x;
  let uy = last.d.y - last.c.y;
  let len = Math.hypot(ux, uy);
  if (len < 1e-6) {
    ux = last.d.x - last.a.x;
    uy = last.d.y - last.a.y;
    len = Math.hypot(ux, uy);
  }
  if (len < 1e-6) return { x: 0, y: 1 };
  return { x: ux / len, y: uy / len };
}

/** Append a segment ending at `to`, leaving along the current exit tangent and
 *  arriving along the chord. Handles are a third of the reach, which keeps the
 *  curve's shape independent of how far away the new point is. */
function pathExtend(decl: PathDeclStmt, to: { x: number; y: number }): void {
  const segs = segsFromPathDecl(decl);
  const last = segs[segs.length - 1];
  if (!last) return;
  const u = exitTangent(last);
  const reach = Math.hypot(to.x - last.d.x, to.y - last.d.y);
  if (reach < 1e-6) return;
  const handle = reach / 3;
  const vx = (to.x - last.d.x) / reach;
  const vy = (to.y - last.d.y) / reach;
  segs.push({
    a: { ...last.d },
    b: { x: last.d.x + u.x * handle, y: last.d.y + u.y * handle },
    c: { x: to.x - vx * handle, y: to.y - vy * handle },
    d: { x: to.x, y: to.y }
  });
  rewritePath(decl, segs);
  sandbox.selectedSeg = segs.length - 1;
}

/** Add anchor — continue the last segment's exit tangent ~24 in out. The new
 *  `.a` is the previous `.d` exactly, so the path stays continuous. */
export function pathAddAnchor(decl: PathDeclStmt): void {
  const segs = segsFromPathDecl(decl);
  const last = segs[segs.length - 1];
  if (!last) return;
  const REACH = 24;
  const u = exitTangent(last);
  pathExtend(decl, { x: last.d.x + u.x * REACH, y: last.d.y + u.y * REACH });
}

/** Extend the path to a point picked on the field (double-click on open field,
 *  away from the curve — double-clicking ON the curve inserts an anchor instead). */
export function pathExtendTo(decl: PathDeclStmt, x: number, y: number): void {
  pathExtend(decl, { x: Math.round(x * 10) / 10, y: Math.round(y * 10) / 10 });
}

/** De Casteljau split of a control-point segment at parameter t — simpler
 *  than the anchor model's splitAnchor (spline.ts) because a control point
 *  carries no heading to recover; the split is exactly three lerps down to
 *  the midpoint, twice. `stopEnd` (a property of where the ORIGINAL segment
 *  ended) carries to the right half only; `reversed`/`maxVel`/`maxAccel`
 *  describe the whole original curve, so both halves keep them. */
function splitSegment(seg: SegModel, t: number): [SegModel, SegModel] {
  const lerp = (p: { x: number; y: number }, q: { x: number; y: number }) => ({
    x: p.x + (q.x - p.x) * t,
    y: p.y + (q.y - p.y) * t
  });
  const ab = lerp(seg.a, seg.b);
  const bc = lerp(seg.b, seg.c);
  const cd = lerp(seg.c, seg.d);
  const abc = lerp(ab, bc);
  const bcd = lerp(bc, cd);
  const m = lerp(abc, bcd);
  const left: SegModel = { a: seg.a, b: ab, c: abc, d: m, reversed: seg.reversed, maxVel: seg.maxVel, maxAccel: seg.maxAccel };
  const right: SegModel = { a: m, b: bcd, c: cd, d: seg.d, reversed: seg.reversed, stopEnd: seg.stopEnd, maxVel: seg.maxVel, maxAccel: seg.maxAccel };
  return [left, right];
}

/** Insert midpoint — split the selected segment (sandbox.selectedSeg), or the
 *  last one if none is selected, at t = 0.5. */
export function pathInsertMidpoint(decl: PathDeclStmt): void {
  const segs = segsFromPathDecl(decl);
  if (!segs.length) return;
  let i = sandbox.selectedSeg;
  if (i === null || i < 0 || i >= segs.length) i = segs.length - 1;
  const [left, right] = splitSegment(segs[i], 0.5);
  segs.splice(i, 1, left, right);
  rewritePath(decl, segs);
  sandbox.selectedSeg = i;
}

/** Insert an anchor at the nearest point on the curve — the field's
 *  double-click handler resolves (segIndex, t) by dense-sampling, then hands
 *  both straight to the same split used by the Insert midpoint button. Not
 *  named in the Feature 3 brief's own list, but it writes through
 *  rewritePath() just like the rest of the structural ops, so it gets the
 *  same undo coverage for free. */
export function pathInsertAt(decl: PathDeclStmt, segIndex: number, t: number): void {
  const segs = segsFromPathDecl(decl);
  if (segIndex < 0 || segIndex >= segs.length) return;
  const [left, right] = splitSegment(segs[segIndex], Math.max(0.02, Math.min(0.98, t)));
  segs.splice(segIndex, 1, left, right);
  rewritePath(decl, segs);
  sandbox.selectedSeg = segIndex;
}

/** Delete anchor — remove a joint by merging the two segments that share it
 *  (segs.selectedSeg and its neighbour, or the last two if none is selected
 *  / the selection is the final segment), reusing the outer handles of the
 *  merged pair. Refuses when only one segment is left — there is no joint
 *  left to remove. */
export function pathDeleteAnchor(decl: PathDeclStmt): void {
  const segs = segsFromPathDecl(decl);
  if (segs.length <= 1) return;
  let i = sandbox.selectedSeg;
  if (i === null || i < 0 || i >= segs.length - 1) i = segs.length - 2;
  const merged: SegModel = {
    a: segs[i].a,
    b: segs[i].b,
    c: segs[i + 1].c,
    d: segs[i + 1].d,
    reversed: segs[i].reversed,
    stopEnd: segs[i + 1].stopEnd,
    maxVel: segs[i].maxVel,
    maxAccel: segs[i].maxAccel
  };
  segs.splice(i, 2, merged);
  rewritePath(decl, segs);
  sandbox.selectedSeg = null;
}

export type MirrorAxis = 'horizontal' | 'vertical' | 'diagonal' | 'antidiagonal' | 'rotate180';

/** Mirror / rotate — control points carry no heading, so each is a plain
 *  coordinate map over every point of every segment. VEX fields are
 *  diagonally AND rotationally symmetric, so the diagonal reflections and
 *  the 180° rotation are as legitimate a "same field, other alliance/side"
 *  transform as the horizontal/vertical flips. */
export function pathMirror(decl: PathDeclStmt, axis: MirrorAxis): void {
  const map = (p: { x: number; y: number }): { x: number; y: number } => {
    switch (axis) {
      case 'horizontal':
        return { x: -p.x, y: p.y };
      case 'vertical':
        return { x: p.x, y: -p.y };
      case 'diagonal':
        return { x: p.y, y: p.x };
      case 'antidiagonal':
        return { x: -p.y, y: -p.x };
      case 'rotate180':
        return { x: -p.x, y: -p.y };
    }
  };
  const segs = segsFromPathDecl(decl).map((s) => ({ ...s, a: map(s.a), b: map(s.b), c: map(s.c), d: map(s.d) }));
  rewritePath(decl, segs);
}

/** Reverse direction — reverse the segment order and swap `a` with `d` and
 *  `b` with `c` within each. `.reversed` is left alone: that flag means
 *  "drive this piece backward", a separate idea from which end of the path
 *  is the start, so flipping the path's own direction doesn't imply flipping
 *  it too. `.stopEnd`/`.maxVel`/`.maxAccel` stay with their own segment (now
 *  in its new position), rather than migrating to whichever segment ends at
 *  the same physical point. */
export function pathReverse(decl: PathDeclStmt): void {
  const segs = segsFromPathDecl(decl)
    .slice()
    .reverse()
    .map((s) => ({
      a: s.d,
      b: s.c,
      c: s.b,
      d: s.a,
      reversed: s.reversed,
      stopEnd: s.stopEnd,
      maxVel: s.maxVel,
      maxAccel: s.maxAccel
    }));
  rewritePath(decl, segs);
  sandbox.selectedSeg = null;
}

/** The robot's simulated pose right before statement `idx` — the last
 *  movement's end pose, or the nearest preceding chassis.setPos if that came
 *  later than the last movement (a bare re-seed with nothing simulated after
 *  it yet), or the route's start pose if neither exists. */
function poseBeforeStmt(idx: number): Pose | null {
  const { parsed, sim } = getRoute();
  let bestSeg: { i: number; pose: Pose } | null = null;
  for (const seg of sim.segments) {
    if (seg.stmtIndex < idx && (!bestSeg || seg.stmtIndex > bestSeg.i)) bestSeg = { i: seg.stmtIndex, pose: seg.end };
  }
  let setPosIdx = -1;
  let setPosPose: Pose | null = null;
  for (let i = 0; i < idx; i++) {
    const s = parsed.stmts[i];
    if (s.kind === 'call' && s.fn === 'chassis.setPos') {
      const num = (k: number) => {
        const a = (s as CallStmt).args[k];
        return a?.kind === 'num' ? a.lit.value : 0;
      };
      setPosIdx = i;
      setPosPose = { x: num(0), y: num(1), h: num(2) };
    }
  }
  if (setPosIdx > (bestSeg?.i ?? -1)) return setPosPose;
  return bestSeg?.pose ?? sim.startPose;
}

/**
 * Creating a spline from nothing — the entry point for the whole path-panel
 * feature. `callIdx` is a ramsete.trajectory(...) call whose path argument is
 * an identifier with no matching declaration yet (currently just a parse
 * error). Inserts `std::vector<BezierSegment> <name> = { … };` immediately
 * above the call.
 *
 * The seed is ONE quadratic: it starts at the robot's simulated pose at that
 * point in the route and LEAVES ALONG ITS HEADING — a seed that started off
 * the robot's facing would ask RAMSETE to turn in place at t=0 — then runs 24
 * inches out, ending in the direction of the field origin. So the curve bends
 * from wherever the robot happens to be pointing toward the middle of the
 * field, which is where a route is usually headed next.
 *
 * A quadratic (a, P, d) is stored as its exact cubic degree-elevation, since
 * BezierSegment is cubic: b = a + ⅔(P−a), c = d + ⅔(P−d). P sits half a chord
 * along the heading ray, so a robot already facing the origin seeds a dead
 * straight line (P lands on the midpoint) and everything else seeds an arc.
 *
 * Splices sandbox.code directly rather than through rewritePath (there's no
 * existing decl to rewrite yet), so it pushes its own undo step (Feature 3).
 */
export function createSplineFor(callIdx: number): void {
  const { parsed } = getRoute();
  const call = parsed.stmts[callIdx];
  if (!call || call.kind !== 'call' || call.fn !== 'ramsete.trajectory') return;
  const a0 = call.args[0];
  if (a0?.kind !== 'ident') return;
  const name = a0.name;

  const pose = poseBeforeStmt(callIdx) ?? { x: 0, y: 0, h: 0 };
  const REACH = 24;
  const h = pose.h * (Math.PI / 180);
  const hx = Math.sin(h);
  const hy = Math.cos(h);
  const a = { x: pose.x, y: pose.y };

  // Aim at the origin. Sitting exactly on it leaves no direction to take, so
  // fall back to running straight out along the heading.
  const oLen = Math.hypot(a.x, a.y);
  const ox = oLen > 1e-6 ? -a.x / oLen : hx;
  const oy = oLen > 1e-6 ? -a.y / oLen : hy;
  const d = { x: a.x + ox * REACH, y: a.y + oy * REACH };

  // The quadratic's control point: half a chord along the heading ray.
  const p = { x: a.x + hx * (REACH / 2), y: a.y + hy * (REACH / 2) };
  const segs: SegModel[] = [
    {
      a,
      b: { x: a.x + ((p.x - a.x) * 2) / 3, y: a.y + ((p.y - a.y) * 2) / 3 },
      c: { x: d.x + ((p.x - d.x) * 2) / 3, y: d.y + ((p.y - d.y) * 2) / 3 },
      d
    }
  ];
  const body = segs.map((s) => '  ' + fmtSeg(s)).join(',\n');
  const decl = `std::vector<BezierSegment> ${name} = {\n${body}\n};\n`;
  const lineStart = sandbox.code.lastIndexOf('\n', call.start - 1) + 1;
  pushUndo();
  sandbox.code = sandbox.code.slice(0, lineStart) + decl + sandbox.code.slice(lineStart);
  // the new decl becomes one whole statement ahead of the call it seeds
  sandbox.selected = callIdx + 1;
}

/** Reset the ACTIVE tab's code back to the starter route — the "reset"
 *  button. Pushes its own undo step (a discrete, whole-buffer replacement,
 *  like pursuit's loadTemplate) so ⌘Z can bring the previous text back. */
export function resetSandboxCode() {
  pushUndo();
  sandbox.code = DEFAULT_CODE;
  sandbox.efforts = {};
  sandbox.selected = null;
  sandbox.time = 0;
  sandbox.playing = false;
}

// --- tab management (Feature 1) ----------------------------------------------

/** Copy the live mirrored buffer into its own tab record. The persist effect
 *  above does this too on every keystroke, but switching tabs needs it to
 *  happen synchronously and before activeTab moves — see switchTab. */
function stashActiveTab(): void {
  const t = sandbox.tabs[sandbox.activeTab];
  if (!t) return;
  t.code = sandbox.code;
  t.knobs = { ...sandbox.knobs };
  t.efforts = { ...sandbox.efforts };
  t.field = sandbox.field;
}

/** Mirror tabs[activeTab] into the live buffer everything else reads
 *  (sandbox.code/knobs/efforts) — the other half of the swap above. */
function hydrateActiveTab(): void {
  const t = sandbox.tabs[sandbox.activeTab];
  if (!t) return;
  sandbox.code = t.code;
  sandbox.knobs = { ...t.knobs };
  sandbox.efforts = { ...t.efforts };
  sandbox.field = t.field;
}

/** Selection/playback state means nothing carried over from a different
 *  tab's statements — cleared after every tab-changing operation below. */
function resetTransientUiState(): void {
  sandbox.selected = null;
  sandbox.selectedSeg = null;
  sandbox.selectedAnchor = null;
  sandbox.time = 0;
  sandbox.playing = false;
}

/**
 * Switch the focused tab: stash the outgoing buffer into its own record,
 * move activeTab, then hydrate the live buffer from the incoming tab.
 * Undo/redo and cleanSig live ON each tab already, so switching never has to
 * touch them — that's what makes undo per-tab "for free" here.
 */
export function switchTab(index: number): void {
  if (index < 0 || index >= sandbox.tabs.length || index === sandbox.activeTab) return;
  stashActiveTab();
  sandbox.activeTab = index;
  hydrateActiveTab();
  resetTransientUiState();
}

/**
 * Close tab `i`. Does NOT confirm — the caller (SandboxCode's × handler)
 * checks isTabDirty and confirms first, the same split the old
 * confirmDiscard/loadAuton pairing already used (the store performs the
 * mutation, the component owns the confirm() dialog). Closing the active tab
 * hydrates whichever tab slides into its place; closing the last tab leaves
 * one fresh untitled tab rather than an empty strip.
 */
export function closeTab(i: number): void {
  if (!sandbox.tabs[i]) return;
  const wasActive = i === sandbox.activeTab;
  sandbox.tabs.splice(i, 1);
  if (sandbox.tabs.length === 0) sandbox.tabs.push(makeTab(sandbox.field));
  if (wasActive) {
    sandbox.activeTab = Math.min(i, sandbox.tabs.length - 1);
    hydrateActiveTab();
    resetTransientUiState();
  } else if (i < sandbox.activeTab) {
    sandbox.activeTab--;
  }
}

/** New — open a fresh untitled tab from the starter code and focus it. Tabs
 *  are additive: New never touches any other tab's buffer, unlike the old
 *  single-document newAuton this replaces. */
export function newAuton(): void {
  stashActiveTab();
  sandbox.tabs.push(makeTab(sandbox.field));
  sandbox.activeTab = sandbox.tabs.length - 1;
  hydrateActiveTab();
  resetTransientUiState();
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

/** Serialize the current route (name + knobs + efforts header, then code). */
export function serializeRoute(name?: string): string {
  const meta = {
    name,
    knobs: { ...sandbox.knobs },
    efforts: { ...sandbox.efforts },
    field: sandbox.field,
    // Stamped so parseRouteFile can tell a file written under an older knob
    // meaning from a current one. A file saved before this existed has no
    // version, which is exactly the case that needs migrating.
    knobsVersion: KNOBS_VERSION
  };
  return `${PARAM_PREFIX}${JSON.stringify(meta)}\n${sandbox.code}`;
}

/** Parse a route text file's `//!params` header (if present) into
 *  knobs/efforts, migrating knobsVersion exactly like load() does for the
 *  localStorage blob — a saved FILE can predate the current knob meanings
 *  just as easily as a browser session can. Pure: returns a fresh
 *  {code, knobs, efforts} instead of writing anywhere, so the live-buffer
 *  loader (loadRouteText) and the new-tab loader (tabFromRouteText) share one
 *  implementation of the round-trip instead of two. */
function parseRouteFile(text: string): {
  code: string;
  knobs: SimKnobs;
  efforts: EffortOverrides;
  /** null when the header carries no field — the caller decides what to
   *  inherit rather than this silently picking one, since a file written
   *  before autons had their own field says nothing about which it wants. */
  field: FieldBackdrop | null;
} {
  const normalized = text.replace(/\r\n?/g, '\n');
  const nl = normalized.indexOf('\n');
  const firstLine = nl === -1 ? normalized : normalized.slice(0, nl);
  let code = normalized;
  const knobs: SimKnobs = { ...DEFAULT_KNOBS };
  let efforts: EffortOverrides = {};
  let field: FieldBackdrop | null = null;
  if (firstLine.startsWith(PARAM_PREFIX)) {
    try {
      // A stale header may still carry a "trajectories" key from the retired
      // [TRAJECTORY] marker — it's simply not read here, so old saved autons
      // still load fine, just without any spline data (there is none anymore;
      // a path is real code below the header now).
      const meta = JSON.parse(firstLine.slice(PARAM_PREFIX.length)) as {
        knobs?: Record<string, number | boolean>;
        efforts?: EffortOverrides;
        knobsVersion?: number;
        field?: FieldBackdrop;
      };
      if (meta.knobs) {
        for (const k of Object.keys(DEFAULT_KNOBS) as (keyof SimKnobs)[]) {
          const v = meta.knobs[k];
          if (typeof v === typeof DEFAULT_KNOBS[k]) (knobs as unknown as Record<string, number | boolean>)[k] = v!;
        }
        // A saved file carries knobs written under whatever version was current
        // when it was saved, so it needs the same migration localStorage gets in
        // load(). Without this an auton saved before version 6 hands over a
        // WEIGHT IN POUNDS and it is silently read as that many kilograms — the
        // numbers are both plausible, so nothing looks wrong; the robot just
        // accelerates half as hard.
        if (meta.knobsVersion !== KNOBS_VERSION) migrateKnobs(knobs);
        clampKnobs(knobs);
      }
      if (meta.efforts) efforts = { ...meta.efforts };
      // normalize so a hand-edited or renamed backdrop id can't leave the
      // sandbox pointing at a PNG that isn't there
      if (meta.field) field = normalizeBackdropId(meta.field);
    } catch {
      // malformed header — treat the whole thing as code
    }
    code = nl === -1 ? '' : normalized.slice(nl + 1);
  }
  return { code: migrateCode(code), knobs, efforts, field };
}

/** Build a brand-new tab from a saved auton's file text — used by loadAuton
 *  when the file isn't already open in a tab. Clean the moment it's created
 *  (it matches what's on disk by definition) and starts with empty
 *  undo/redo: a freshly opened document has no prior state in THIS tab to
 *  undo into. */
function tabFromRouteText(text: string, name: string): TabState {
  const r = parseRouteFile(text);
  // A file saved before autons carried a field falls back to whatever the tab
  // you opened it from is on, the same inheritance makeTab uses.
  const field = r.field ?? sandbox.field;
  return {
    id: makeId(),
    name,
    code: r.code,
    knobs: r.knobs,
    efforts: r.efforts,
    field,
    cleanSig: tabSignature(r.code, r.knobs, r.efforts, field),
    undo: [],
    redo: []
  };
}

/** Load a route text file into the ACTIVE tab's live buffer in place — kept
 *  for the same round-trip parseRouteFile backs (do not break it: knobs are
 *  migrated on knobsVersion here exactly as they are for tabFromRouteText and
 *  the localStorage load() path). Every real "open" now goes through
 *  loadAuton -> tabFromRouteText (a NEW tab) instead of this, but it stays
 *  exported for anything that legitimately wants to replace the CURRENT
 *  tab's content outright. */
export function loadRouteText(text: string) {
  const r = parseRouteFile(text);
  sandbox.code = r.code;
  sandbox.knobs = r.knobs;
  sandbox.efforts = r.efforts;
  sandbox.field = r.field ?? sandbox.field;
  sandbox.selected = null;
  sandbox.time = 0;
  sandbox.playing = false;
  // loading a route text file is itself a clean checkpoint.
  markClean();
}

// --- saved autons (folder-backed) ----------------------------------------------
// Saved autons are real .txt files in the "sandbox autons" folder, served by the
// /api/autons dev endpoint. The picker drawer is the folder listing; saving
// writes a file. Opening a file focuses its tab if one is already open,
// otherwise opens a new one (Feature 1) — see loadAuton below.

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

/** Write the current buffer to a file in the folder (name = file name). The
 *  low-level primitive both saveCurrentAuton and saveAutonAs write through —
 *  neither one prompts, so this is the only place that talks to the API. */
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

/** Save — writes back to the open document, no name prompt. A no-op when
 *  untitled (nothing to overwrite); the header's Save button falls through
 *  to Save as in that case instead of calling this (see SandboxCode's onSave). */
export async function saveCurrentAuton(): Promise<void> {
  const tab = sandbox.tabs[sandbox.activeTab];
  if (!tab?.name) return;
  await saveAuton(tab.name);
  markClean();
}

/** Save as — the only place a name is typed. Always renames the ACTIVE TAB
 *  to the new name, whether or not a document was already open, so "Save
 *  as" reliably means "this tab is now that file." */
export async function saveAutonAs(name: string): Promise<void> {
  const trimmed = name.trim() || 'untitled';
  await saveAuton(trimmed);
  const tab = sandbox.tabs[sandbox.activeTab];
  if (tab) tab.name = trimmed;
  markClean();
}

/**
 * Open a saved auton (Feature 1). Already open in a tab → focus it instead
 * of opening a second copy; otherwise stash the current buffer, append a
 * fresh tab loaded from the file, and focus that. Callers that care about
 * confirming a dirty CLOSE (SandboxCode's × handler) do so themselves —
 * opening never loses anything, since it only ever adds or focuses a tab.
 */
export function loadAuton(name: string): void {
  const existing = sandbox.tabs.findIndex((t) => t.name === name);
  if (existing !== -1) {
    switchTab(existing);
    return;
  }
  const a = sandbox.saved.find((s) => s.name === name);
  if (!a) return;
  stashActiveTab();
  sandbox.tabs.push(tabFromRouteText(a.content, name));
  sandbox.activeTab = sandbox.tabs.length - 1;
  hydrateActiveTab();
  resetTransientUiState();
}

/** Delete a saved auton's file. Any tab still open under that name simply
 *  keeps editing an in-memory copy — Save would recreate the file, same as
 *  any text editor after its file disappears out from under it. */
export async function deleteAuton(name: string): Promise<void> {
  try {
    await fetch(`${AUTONS_API}?name=${encodeURIComponent(name)}`, { method: 'DELETE' });
  } catch {
    // ignore
  }
  await refreshSavedAutons();
}
