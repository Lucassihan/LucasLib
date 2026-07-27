// Pure Pursuit planner state — a cubic bezier spline you shape directly
// (anchors + tangent handles), the planning knobs, and the baked PathPoint
// array they produce. The planner runs the whole pre-calculation here
// (sample the spline → arc-length resample → curvature → speed passes) so the
// robot's followPath() just chases a finished array. Persists to localStorage.

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
  lock?: boolean; // symmetry lock — the two handle lengths stay equal
  vCap?: number; // cap planned speed here to N in/s (0 = full stop); undefined = no cap
}

export interface PursuitPlanParams {
  grip: number; // traction cap: v ≤ sqrt(grip / |κ|) = sqrt(grip·r); higher = corner faster
  maxVel: number; // top planned speed, in/s
  maxAccel: number; // planning accel for the slow-down pass, in/s^2
}

export const DEFAULT_PARAMS: PursuitPlanParams = {
  grip: 100,
  maxVel: 60,
  maxAccel: 60
};

/** Fixed export point spacing (in). The profile is exact internally (~0.5"); the
 *  exported array is just resampled at this arc rhythm — no longer a user knob. */
const SPACING = 2;

const DEFAULT_ANCHORS: Anchor[] = [
  { x: -48, y: -60, ang: 0, out: 26, in: 26 },
  { x: -36, y: 2, ang: 38, out: 22, in: 22 },
  { x: 14, y: 30, ang: 68, out: 20, in: 20 },
  { x: 52, y: 44, ang: 52, out: 18, in: 18 }
];

interface Persisted {
  anchors: Anchor[];
  params: PursuitPlanParams;
  name: string;
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

/** Drop fields left behind by the removed mid-path-turn feature. */
function migrateAnchor(a: Anchor): Anchor {
  const rest = { ...a } as Anchor & { turn?: unknown; inAng?: unknown };
  delete rest.turn;
  delete rest.inAng;
  return rest;
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

function load(): { anchors: Anchor[]; params: PursuitPlanParams; name: string } {
  if (typeof localStorage !== 'undefined') {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (raw) {
        const p = JSON.parse(raw) as Partial<Persisted>;
        let anchors: Anchor[] | null = null;
        if (Array.isArray(p.anchors) && p.anchors.length >= 2) {
          // spread-preserve so optional fields (lock, and later structural ones)
          // survive a reload; only require the five core numbers to be finite
          const kept = p.anchors.filter(
            (a) => a && [a.x, a.y, a.ang, a.out, a.in].every((n) => typeof n === 'number' && isFinite(n))
          );
          if (kept.length >= 2) anchors = kept.map((a) => migrateAnchor({ ...a }));
        }
        if (!anchors && Array.isArray(p.waypoints) && p.waypoints.length >= 2) {
          anchors = anchorsThrough(p.waypoints); // migrate the pre-bezier format
        }
        if (anchors) {
          return {
            anchors,
            params: { ...DEFAULT_PARAMS, ...(p.params ?? {}) },
            name: typeof p.name === 'string' ? p.name : 'path'
          };
        }
      }
    } catch {
      // corrupt — reset
    }
  }
  return { anchors: DEFAULT_ANCHORS.map((a) => ({ ...a })), params: { ...DEFAULT_PARAMS }, name: 'path' };
}

const initial = load();

export const pursuit = $state<{
  anchors: Anchor[];
  params: PursuitPlanParams;
  name: string;
  selected: number | null; // selected anchor index, or null
}>({
  anchors: initial.anchors,
  params: initial.params,
  name: initial.name,
  selected: null
});

// persist on any change
$effect.root(() => {
  $effect(() => {
    if (typeof localStorage === 'undefined') return;
    const data: Persisted = {
      anchors: pursuit.anchors.map((a) => ({ ...a })),
      params: { ...pursuit.params },
      name: pursuit.name
    };
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(data));
    } catch {
      // quota — ignore
    }
  });
});

// --- view options (not part of the path — editor UI prefs, persisted apart) ----
const VIEW_KEY = 'lucaslib-debugger.pursuit-view';

export interface ViewOpts {
  snap: number; // grid snap size, inches: 0 = off · 24 tile · 12 half · 6 quarter
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
// A snapshot is the whole editable path (anchors + params + name). Discrete edits
// push before mutating; rapid same-key edits (a slider being dragged, arrow-key
// nudges) coalesce into one entry so one gesture = one undo step.

interface Snapshot {
  anchors: Anchor[];
  params: PursuitPlanParams;
  name: string;
}

let undoStack: Snapshot[] = [];
let redoStack: Snapshot[] = [];
let lastKey = '';
let lastAt = 0;
const HIST_CAP = 120;
const COALESCE_MS = 700;

/** Reactive flags for the toolbar buttons. */
export const history = $state({ canUndo: false, canRedo: false });
function syncHist() {
  history.canUndo = undoStack.length > 0;
  history.canRedo = redoStack.length > 0;
}

function snap(): Snapshot {
  return { anchors: pursuit.anchors.map((a) => ({ ...a })), params: { ...pursuit.params }, name: pursuit.name };
}
function apply(s: Snapshot) {
  pursuit.anchors = s.anchors.map((a) => ({ ...a }));
  pursuit.params = { ...s.params };
  pursuit.name = s.name;
  if (pursuit.selected !== null && pursuit.selected >= pursuit.anchors.length) pursuit.selected = null;
}

/**
 * Record the current state onto the undo stack before a change. Pass a
 * `coalesceKey` for continuous edits (slider drag, arrow nudge) so a burst of
 * same-key pushes within {@link COALESCE_MS} collapses into a single step.
 */
export function pushHistory(coalesceKey = '') {
  const now = Date.now();
  if (coalesceKey && coalesceKey === lastKey && now - lastAt < COALESCE_MS && undoStack.length) {
    lastAt = now;
    redoStack = [];
    syncHist();
    return;
  }
  undoStack.push(snap());
  if (undoStack.length > HIST_CAP) undoStack.shift();
  redoStack = [];
  lastKey = coalesceKey;
  lastAt = now;
  syncHist();
}

export function undo() {
  if (!undoStack.length) return;
  redoStack.push(snap());
  apply(undoStack.pop()!);
  lastKey = '';
  syncHist();
}
export function redo() {
  if (!redoStack.length) return;
  undoStack.push(snap());
  apply(redoStack.pop()!);
  lastKey = '';
  syncHist();
}

/** Set a shaping/follow param, coalescing rapid changes to the same knob. */
export function setParam<K extends keyof PursuitPlanParams>(k: K, v: PursuitPlanParams[K]) {
  pushHistory('param:' + k);
  pursuit.params[k] = v;
}

// --- speed → color -------------------------------------------------------------
// Traffic-light ramp: red (slow) → yellow (mid) → green (fast). Colored across
// the path's OWN speed span (see speedRange) so the whole gradient shows instead
// of saturating green — shared by the field line and the velocity strip.
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
 * The colour window for a plan: the path's actual [min, max] speed, so the
 * slowest point reads red and the fastest green. Falls back to absolute
 * [0, maxVel] for a near-constant-speed path (nothing to spread across).
 */
export function speedRange(points: { v: number }[], maxVel: number): { lo: number; hi: number } {
  let lo = Infinity;
  let hi = -Infinity;
  for (const p of points) {
    if (p.v < lo) lo = p.v;
    if (p.v > hi) hi = p.v;
  }
  if (!isFinite(lo) || hi - lo < 3) return { lo: 0, hi: Math.max(1, maxVel) };
  return { lo, hi };
}

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
 * Exact signed curvature of cubic segment `c` at parameter t — Echo's form,
 * κ = (x'·y'' − y'·x'') / |p'|³ straight from the bezier derivatives, instead of
 * a Menger three-point estimate off the resampled points (no resampling noise,
 * and stable at any sampling density). Sign is unused here; the profile takes |κ|.
 */
function curvature(c: [Pt, Pt, Pt, Pt], t: number): number {
  const u = 1 - t;
  const dx = 3 * u * u * (c[1].x - c[0].x) + 6 * u * t * (c[2].x - c[1].x) + 3 * t * t * (c[3].x - c[2].x);
  const dy = 3 * u * u * (c[1].y - c[0].y) + 6 * u * t * (c[2].y - c[1].y) + 3 * t * t * (c[3].y - c[2].y);
  const ddx = 6 * u * (c[2].x - 2 * c[1].x + c[0].x) + 6 * t * (c[3].x - 2 * c[2].x + c[1].x);
  const ddy = 6 * u * (c[2].y - 2 * c[1].y + c[0].y) + 6 * t * (c[3].y - 2 * c[2].y + c[1].y);
  const sp = dx * dx + dy * dy;
  return sp < 1e-9 ? 0 : (dx * ddy - dy * ddx) / Math.pow(sp, 1.5);
}

// --- the planning pipeline ------------------------------------------------------
// dense-sample the spline (~0.5" vertices) with EXACT curvature → cap speed by
// v = min(vmax, kF/κ) at every dense sample → backward decel pass to 0 at the end
// → then resample the finished profile at the export `spacing` (Echo's 2" rhythm).
// The profile lives at dense resolution so slow-downs are sharp; the exported
// PathPoint array is the coarse resample and the follower interpolates it.

export interface BakedPoint {
  x: number;
  y: number;
  v: number; // planned speed, in/s
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
}

export function buildPath(anchors: Anchor[], params: PursuitPlanParams): Plan {
  if (anchors.length < 2) return { points: [], dense: [], total: 0 };
  const { grip, maxVel, maxAccel } = params;

  // 1. dense-sample every segment (~0.5" vertices) with exact curvature per sample
  const dense: DenseSample[] = [];
  for (let i = 0; i < anchors.length - 1; i++) {
    const c = segControls(anchors[i], anchors[i + 1]);
    const polyLen = dist(c[0], c[1]) + dist(c[1], c[2]) + dist(c[2], c[3]);
    const n = Math.min(400, Math.max(16, Math.ceil(polyLen / 0.5)));
    for (let k = i === 0 ? 0 : 1; k <= n; k++) {
      const t = k / n;
      dense.push({ ...bez(c, t), seg: i, t, s: 0, k: curvature(c, t), v: maxVel });
    }
  }

  // 2. arc length along the dense samples
  const N = dense.length;
  for (let i = 1; i < N; i++) dense[i].s = dense[i - 1].s + dist(dense[i - 1], dense[i]);
  const total = N ? dense[N - 1].s : 0;

  // 3. speed profile AT DENSE RESOLUTION: traction cap v ≤ sqrt(grip/|κ|) =
  //    sqrt(grip·r) (the no-slip cap, same shape as the boomerang's), then
  //    per-anchor speed caps, then accel/decel passes.
  for (let i = 0; i < N; i++) {
    const kk = Math.abs(dense[i].k);
    dense[i].v = kk > 1e-6 ? Math.min(maxVel, Math.sqrt(grip / kk)) : maxVel;
  }
  // per-anchor caps (0 = full stop)
  const hasCaps = anchors.some((a) => a.vCap !== undefined);
  if (hasCaps) {
    for (const a of anchors) {
      const cap = a.vCap;
      if (cap === undefined) continue;
      for (let i = 0; i < N; i++) if (Math.hypot(dense[i].x - a.x, dense[i].y - a.y) < 1.5) dense[i].v = Math.min(dense[i].v, cap);
    }
  }
  if (N) dense[N - 1].v = 0;
  // forward accel pass (only when caps exist — ramps back up out of a stop/cap;
  // a no-op for uncapped paths, so their profile is unchanged)
  if (hasCaps) {
    for (let i = 1; i < N; i++) {
      const d = dense[i].s - dense[i - 1].s;
      dense[i].v = Math.min(dense[i].v, Math.sqrt(dense[i - 1].v * dense[i - 1].v + 2 * maxAccel * d));
    }
  }
  // backward decel pass so the robot can always brake to the next cap / the end
  for (let i = N - 2; i >= 0; i--) {
    const d = dense[i + 1].s - dense[i].s;
    dense[i].v = Math.min(dense[i].v, Math.sqrt(dense[i + 1].v * dense[i + 1].v + 2 * maxAccel * d));
  }

  // 4. export array: resample the finished dense profile at the fixed arc SPACING
  //    (Echo's ceil(len / 2") rhythm). The follower interpolates it.
  const step = SPACING;
  const points: BakedPoint[] = [];
  let di = 0;
  for (let target = 0; target < total; target += step) {
    while (di < N - 1 && dense[di + 1].s < target) di++;
    const span = dense[di + 1].s - dense[di].s;
    const u = span > 1e-9 ? (target - dense[di].s) / span : 0;
    points.push({
      x: dense[di].x + (dense[di + 1].x - dense[di].x) * u,
      y: dense[di].y + (dense[di + 1].y - dense[di].y) * u,
      v: dense[di].v + (dense[di + 1].v - dense[di].v) * u,
      s: target
    });
  }
  if (N) points.push({ x: dense[N - 1].x, y: dense[N - 1].y, v: dense[N - 1].v, s: total });
  return { points, dense, total };
}

/** Total traversal time of a baked plan (seconds) — matches the field clock:
 *  dt = ds / avg-speed, with the same 2 in/s floor so the near-zero end speed
 *  can't blow the clock up. */
export function planDuration(points: BakedPoint[]): number {
  let t = 0;
  for (let i = 1; i < points.length; i++) {
    const ds = points[i].s - points[i - 1].s;
    const vAvg = Math.max(2, (points[i].v + points[i - 1].v) / 2);
    t += ds / vAvg;
  }
  return t;
}

// memoized plan — recomputed only when anchors or shaping params change
let cacheKey = '';
let cacheVal: Plan = { points: [], dense: [], total: 0 };

/** The baked path for the current spline + params (memoized, reactive). */
export function getPlan(): Plan {
  const p = pursuit.params;
  const key = JSON.stringify(pursuit.anchors) + '|' + [p.grip, p.maxVel, p.maxAccel].join(',');
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
  if (a.lock) a.in = a.out = round(len); // symmetry lock keeps both lengths equal
}

/** Set exact anchor fields from typed input (coordinate / heading / handle boxes). */
export function editAnchor(i: number, patch: Partial<Anchor>) {
  const a = pursuit.anchors[i];
  if (a === undefined) return;
  pushHistory('edit:' + i);
  if (patch.x !== undefined) a.x = round(patch.x);
  if (patch.y !== undefined) a.y = round(patch.y);
  if (patch.ang !== undefined) a.ang = round(norm(patch.ang));
  if (patch.out !== undefined) {
    a.out = round(Math.max(3, patch.out));
    if (a.lock) a.in = a.out;
  }
  if (patch.in !== undefined) {
    a.in = round(Math.max(3, patch.in));
    if (a.lock) a.out = a.in;
  }
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

/** Reset an anchor's tangent handle lengths to the chord-based default. */
export function autoHandles(i: number) {
  const a = pursuit.anchors[i];
  if (!a) return;
  pushHistory('auto:' + i);
  const prev = pursuit.anchors[i - 1];
  const next = pursuit.anchors[i + 1];
  if (next) a.out = round(Math.max(3, dist(a, next) * 0.35));
  if (prev) a.in = round(Math.max(3, dist(prev, a) * 0.35));
  if (a.lock) a.in = a.out = round(Math.max(a.in, a.out));
}

/** Set (or clear, with null) the per-anchor speed cap. 0 = full stop. */
export function setAnchorCap(i: number, cap: number | null) {
  const a = pursuit.anchors[i];
  if (!a) return;
  pushHistory('cap:' + i);
  if (cap === null) delete a.vCap;
  else a.vCap = Math.max(0, round(cap));
}

/** Toggle the symmetry lock; turning it on averages the two handle lengths. */
export function toggleLock(i: number) {
  const a = pursuit.anchors[i];
  if (!a) return;
  pushHistory('lock:' + i);
  if (a.lock) {
    delete a.lock;
  } else {
    a.lock = true;
    const m = round(Math.max(3, (a.in + a.out) / 2));
    a.in = m;
    a.out = m;
  }
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

export function deleteAnchor(i: number) {
  if (pursuit.anchors.length <= 2) return; // keep a valid path
  pushHistory();
  pursuit.anchors.splice(i, 1);
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

/** Reverse travel direction: flip anchor order, spin each 180°, swap handles. */
export function reversePath() {
  pushHistory();
  pursuit.anchors = pursuit.anchors
    .slice()
    .reverse()
    .map((a) => ({ ...a, ang: round(norm(a.ang + 180)), in: a.out, out: a.in }));
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

// --- saved paths (file-backed, dev-server /api/paths) -------------------------

export interface SavedPath {
  name: string;
  savedAt: number;
  content: string; // JSON of { anchors, params }
}

export const savedPaths = $state<SavedPath[]>([]);

export async function refreshSavedPaths() {
  try {
    const r = await fetch('/api/paths');
    if (r.ok) {
      const list = (await r.json()) as SavedPath[];
      savedPaths.splice(0, savedPaths.length, ...list);
    }
  } catch {
    // dev server / offline — leave the list as-is
  }
}

function uniquePathName(base: string): string {
  let name = base;
  let n = 2;
  const taken = new Set(savedPaths.map((p) => p.name.toLowerCase()));
  while (taken.has(name.toLowerCase())) name = `${base} ${n++}`;
  return name;
}

/** Write the current path to disk under `name` (defaults to the path name). */
export async function savePath(name?: string) {
  const nm = (name ?? pursuit.name).trim() || 'path';
  pursuit.name = nm;
  const content = JSON.stringify({ anchors: pursuit.anchors, params: pursuit.params });
  try {
    await fetch('/api/paths', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name: nm, content })
    });
  } catch {
    // ignore
  }
  await refreshSavedPaths();
}

/** Save a copy under a fresh "… copy" name without touching the original file. */
export async function duplicatePath() {
  await refreshSavedPaths();
  pursuit.name = uniquePathName(pursuit.name + ' copy');
  await savePath();
}

/** Load a saved path into the editor (undoable). */
export function loadPath(p: SavedPath) {
  try {
    const d = JSON.parse(p.content) as { anchors: Anchor[]; params: Partial<PursuitPlanParams> };
    if (!Array.isArray(d.anchors) || d.anchors.length < 2) return;
    pushHistory();
    pursuit.anchors = d.anchors.map((a) => migrateAnchor({ ...a }));
    pursuit.params = { ...DEFAULT_PARAMS, ...(d.params ?? {}) };
    pursuit.name = p.name;
    pursuit.selected = null;
  } catch {
    // corrupt file — ignore
  }
}

export async function deletePath(name: string) {
  try {
    await fetch('/api/paths?name=' + encodeURIComponent(name), { method: 'DELETE' });
  } catch {
    // ignore
  }
  await refreshSavedPaths();
}

// --- C++ export ---------------------------------------------------------------

/** Generate the paste-ready LucasLib snippet: a PathPoint[] array + the call. */
export function exportCpp(): string {
  const plan = getPlan();
  const name = sanitize(pursuit.name);
  const rows = plan.points.map((pt) => `    {${fmt(pt.x)}, ${fmt(pt.y)}, ${fmt(pt.v)}},`).join('\n');
  const dur = planDuration(plan.points).toFixed(1);

  return `// ${plan.points.length} points \u00b7 ${plan.total.toFixed(1)}" \u00b7 ~${dur}s \u00b7 baked by the Trajectory Generator
static const std::vector<PathPoint> ${name} = {
${rows}
};

followPath(${name});`;
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
