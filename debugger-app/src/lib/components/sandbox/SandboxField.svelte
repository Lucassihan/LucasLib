<script lang="ts">
  import { untrack } from 'svelte';
  import Robot from '../Robot.svelte';
  import Waypoint from '../Waypoint.svelte';
  import TurnOverlay from '../TurnOverlay.svelte';
  import BoomerangOverlay from '../BoomerangOverlay.svelte';
  import { settings, holesToInches } from '$lib/stores/settings.svelte';
  import { sandbox, getRoute, sandboxPoseAt, segmentAt, editNumber, editStructField } from '$lib/stores/sandbox.svelte';
  import {
    getTrajectory,
    ensureTrajectory,
    moveTrajAnchor,
    moveTrajHandle,
    insertTrajAnchor
  } from '$lib/stores/sandbox.svelte';
  import { bakeTrajectory, speedColor, defaultTrajectory, dir as splineDir, type Anchor } from '$lib/trajectory/spline';
  import { toScreen } from '$lib/field/coords';
  import { backdropSrc, fieldImageRect } from '$lib/field/backdrops';
  import { movementColor, isTurn } from '$lib/trace/labels';
  import { base } from '$app/paths';
  import type { CallStmt, BoomDeclStmt } from '$lib/sandbox/parse';
  import type { Movement } from '$lib/trace/types';
  import type { SimSegment } from '$lib/sandbox/sim';

  /** minimal Movement stand-in so replay overlay components render 1:1 */
  function asMovement(seg: SimSegment): Movement {
    return { index: seg.stmtIndex, type: seg.fn } as unknown as Movement;
  }

  const SIZE = 144;
  const fieldRect = fieldImageRect(SIZE);
  const fieldImg = $derived(backdropSrc(settings.fieldBackdrop, base));

  const route = $derived(getRoute());
  const pose = $derived(sandboxPoseAt(sandbox.time));

  const D2R = Math.PI / 180;
  const R2D = 180 / Math.PI;

  // --- handle model -----------------------------------------------------------

  type Role = 'target' | 'heading' | 'distance' | 'carrot' | 'glead' | 'startpos' | 'startheading';
  interface Handle {
    stmtIndex: number;
    role: Role;
    x: number;
    y: number;
    color: string;
    // pivot the handle orbits/extends from — used to draw a connector stem
    ox?: number;
    oy?: number;
  }

  const HEADING_KNOB_R = 13; // in — knob orbit radius for heading handles

  const handles = $derived.by((): Handle[] => {
    const out: Handle[] = [];
    const { parsed, sim } = route;
    const segByStmt = new Map(sim.segments.map((s) => [s.stmtIndex, s]));

    for (let i = 0; i < parsed.stmts.length; i++) {
      const s = parsed.stmts[i];
      if (s.kind !== 'call') continue;
      const seg = segByStmt.get(i);
      const col = movementColor(s.fn);

      const num = (k: number) => (s.args[k]?.kind === 'num' ? (s.args[k] as { lit: { value: number } }).lit.value : null);

      if (s.fn === 'chassis.setPos' && sim.startPose) {
        const x = num(0) ?? 0;
        const y = num(1) ?? 0;
        const h = num(2) ?? 0;
        out.push({ stmtIndex: i, role: 'startpos', x, y, color: '#fbbf24' });
        out.push({
          stmtIndex: i,
          role: 'startheading',
          x: x + HEADING_KNOB_R * Math.sin(h * D2R),
          y: y + HEADING_KNOB_R * Math.cos(h * D2R),
          color: '#fbbf24',
          ox: x,
          oy: y
        });
        continue;
      }
      if (!seg) continue;

      switch (s.fn) {
        case 'moveTo':
        case 'facePoint':
        case 'swingOnLeftToPoint':
        case 'swingOnRightToPoint': {
          const x = num(0);
          const y = num(1);
          if (x !== null && y !== null) out.push({ stmtIndex: i, role: 'target', x, y, color: col });
          break;
        }
        case 'face':
        case 'swingOnLeft':
        case 'swingOnRight': {
          const h = num(0);
          if (h !== null) {
            out.push({
              stmtIndex: i,
              role: 'heading',
              x: seg.end.x + HEADING_KNOB_R * Math.sin(h * D2R),
              y: seg.end.y + HEADING_KNOB_R * Math.cos(h * D2R),
              color: col,
              ox: seg.end.x,
              oy: seg.end.y
            });
          }
          break;
        }
        case 'drive': {
          const d = num(0);
          if (d !== null) {
            // drive(distance, heading, ec, bs) — distance is SIGNED, so the handle
            // lands behind the start for a negative one. No direction flag needed.
            const hh = num(1) ?? seg.start.h;
            out.push({
              stmtIndex: i,
              role: 'distance',
              x: seg.start.x + d * Math.sin(hh * D2R),
              y: seg.start.y + d * Math.cos(hh * D2R),
              color: col,
              ox: seg.start.x,
              oy: seg.start.y
            });
          }
          break;
        }
        case 'boomerangTo': {
          if (seg.boom) {
            const b = seg.boom;
            out.push({ stmtIndex: i, role: 'target', x: b.target.x, y: b.target.y, color: col });
            out.push({
              stmtIndex: i,
              role: 'heading',
              x: b.target.x + HEADING_KNOB_R * Math.sin(b.heading * D2R),
              y: b.target.y + HEADING_KNOB_R * Math.cos(b.heading * D2R),
              color: col,
              ox: b.target.x,
              oy: b.target.y
            });
            out.push({ stmtIndex: i, role: 'carrot', x: b.initCarrot.x, y: b.initCarrot.y, color: col });
            out.push({
              stmtIndex: i,
              role: 'glead',
              x: b.initCarrot.x + (b.target.x - b.initCarrot.x) * b.gLead,
              y: b.initCarrot.y + (b.target.y - b.initCarrot.y) * b.gLead,
              color: col
            });
          }
          break;
        }
      }
    }
    return out;
  });

  // --- drag plumbing ------------------------------------------------------------

  let svgEl: SVGSVGElement | undefined = $state();
  let drag: { stmtIndex: number; role: Role } | null = $state(null);

  // live field coordinate under the cursor (inches), shown as a readout
  let hoverCoord = $state<{ x: number; y: number } | null>(null);

  function toFieldCoords(e: PointerEvent): { x: number; y: number } | null {
    if (!svgEl) return null;
    const m = svgEl.getScreenCTM();
    if (!m) return null;
    const p = new DOMPoint(e.clientX, e.clientY).matrixTransform(m.inverse());
    return { x: p.x - SIZE / 2, y: SIZE / 2 - p.y };
  }

  function onHoverMove(e: PointerEvent) {
    const f = toFieldCoords(e);
    hoverCoord = f ? { x: Math.round(f.x * 10) / 10, y: Math.round(f.y * 10) / 10 } : null;
  }

  function startDrag(e: PointerEvent, h: Handle) {
    e.stopPropagation();
    e.preventDefault();
    drag = { stmtIndex: h.stmtIndex, role: h.role };
    sandbox.selected = h.stmtIndex;
    sandbox.playing = false;
  }

  /** fresh statement + segment after any text rewrite */
  function fresh(stmtIndex: number) {
    const r = getRoute();
    return {
      stmt: r.parsed.stmts[stmtIndex] as CallStmt | BoomDeclStmt | undefined,
      seg: r.sim.segments.find((s) => s.stmtIndex === stmtIndex),
      parsed: r.parsed
    };
  }

  function boomInitOf(stmt: CallStmt | undefined, parsed: ReturnType<typeof getRoute>['parsed']) {
    if (!stmt || stmt.kind !== 'call') return null;
    const a0 = stmt.args[0];
    if (a0?.kind === 'ident') return parsed.booms[a0.name]?.init ?? null;
    if (a0?.kind === 'struct') return a0.init;
    return null;
  }

  const round = (v: number, step: number) => Math.round(v / step) * step;

  function onDragMove(e: PointerEvent) {
    if (!drag) return;
    const f = toFieldCoords(e);
    if (!f) return;
    const { stmtIndex, role } = drag;
    const { stmt, seg, parsed } = fresh(stmtIndex);
    if (!stmt) return;

    if (role === 'startpos' || role === 'target') {
      if (stmt.kind === 'call' && stmt.fn === 'boomerangTo') {
        const init = boomInitOf(stmt, parsed);
        if (!init) return;
        editStructField(init, 'x', round(f.x, 0.1));
        const again = fresh(stmtIndex);
        const init2 = boomInitOf(again.stmt as CallStmt, again.parsed);
        if (init2) editStructField(init2, 'y', round(f.y, 0.1));
      } else if (stmt.kind === 'call') {
        const a0 = stmt.args[0];
        const a1 = stmt.args[1];
        if (a0?.kind === 'num') editNumber(a0.lit, round(f.x, 0.1));
        const again = fresh(stmtIndex).stmt as CallStmt | undefined;
        const b1 = again?.kind === 'call' ? again.args[1] : undefined;
        if (b1?.kind === 'num') editNumber(b1.lit, round(f.y, 0.1));
        void a1;
      }
      return;
    }

    if (role === 'startheading' || role === 'heading') {
      // pivot: setPos/face → robot spot; boomerang → the target point
      let px = 0;
      let py = 0;
      if (stmt.kind === 'call' && stmt.fn === 'chassis.setPos') {
        px = stmt.args[0]?.kind === 'num' ? stmt.args[0].lit.value : 0;
        py = stmt.args[1]?.kind === 'num' ? stmt.args[1].lit.value : 0;
      } else if (stmt.kind === 'call' && stmt.fn === 'boomerangTo') {
        const init = boomInitOf(stmt, parsed);
        const bx = init?.fields.x;
        const by = init?.fields.y;
        px = bx && typeof bx.value === 'number' ? bx.value : 0;
        py = by && typeof by.value === 'number' ? by.value : 0;
      } else if (seg) {
        px = seg.end.x;
        py = seg.end.y;
      }
      let h = Math.atan2(f.x - px, f.y - py) * R2D;
      h = round(h, 1);
      if (h < 0) h += 360;
      if (stmt.kind === 'call' && stmt.fn === 'boomerangTo') {
        const init = boomInitOf(stmt, parsed);
        if (init) editStructField(init, 'heading', h, 0);
      } else if (stmt.kind === 'call') {
        const a = stmt.fn === 'chassis.setPos' ? stmt.args[2] : stmt.args[0];
        if (a?.kind === 'num') editNumber(a.lit, h, 0);
      }
      return;
    }

    if (role === 'distance' && stmt.kind === 'call' && seg) {
      const hh = stmt.args[1]?.kind === 'num' ? stmt.args[1].lit.value : seg.start.h;
      const ux = Math.sin(hh * D2R);
      const uy = Math.cos(hh * D2R);
      const proj = (f.x - seg.start.x) * ux + (f.y - seg.start.y) * uy;
      // signed — dragging the handle behind the start writes a negative distance,
      // which is how drive() reverses
      const d = round(proj, 0.5);
      const a0 = stmt.args[0];
      if (a0?.kind === 'num') editNumber(a0.lit, d);
      return;
    }

    if ((role === 'carrot' || role === 'glead') && stmt.kind === 'call' && seg?.boom) {
      const b = seg.boom;
      const init = boomInitOf(stmt, parsed);
      if (!init) return;
      if (role === 'carrot') {
        // project the pointer onto the lead axis behind the target
        const leadSign = b.backward ? 1 : -1;
        const ux = leadSign * Math.sin(b.heading * D2R);
        const uy = leadSign * Math.cos(b.heading * D2R);
        const proj = (f.x - b.target.x) * ux + (f.y - b.target.y) * uy;
        const initialDist = Math.hypot(b.target.x - seg.start.x, b.target.y - seg.start.y);
        if (initialDist < 1) return;
        const lead = Math.min(1.5, Math.max(0.05, proj / initialDist));
        editStructField(init, 'lead', round(lead, 0.01), 2);
      } else {
        const vx = b.target.x - b.initCarrot.x;
        const vy = b.target.y - b.initCarrot.y;
        const len2 = vx * vx + vy * vy;
        if (len2 < 1e-6) return;
        const u = ((f.x - b.initCarrot.x) * vx + (f.y - b.initCarrot.y) * vy) / len2;
        editStructField(init, 'gLead', round(Math.min(1, Math.max(0, u)), 0.01), 2);
      }
      return;
    }
  }

  // --- zoom + pan --------------------------------------------------------------
  // The svg viewBox becomes a window into the field, scaled by `zoom` and
  // centered on `center` (in viewBox/screen units). getScreenCTM reflects the
  // live viewBox, so handle drags stay pixel-accurate at any zoom.
  let zoom = $state(1);
  let center = $state({ sx: SIZE / 2, sy: SIZE / 2 });
  const ZOOM_MIN = 1;
  const ZOOM_MAX = 6;
  const clampN = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v));

  const view = $derived.by(() => {
    const w = fieldRect.w / zoom;
    const h = fieldRect.h / zoom;
    let x = center.sx - w / 2;
    let y = center.sy - h / 2;
    // edge-clamp like a game camera, except in POV where the robot stays centred
    if (!(follow && povLock)) {
      x = clampN(x, fieldRect.x, fieldRect.x + fieldRect.w - w);
      y = clampN(y, fieldRect.y, fieldRect.y + fieldRect.h - h);
    }
    return { x, y, w, h };
  });

  // handle sizes are authored in screen units and divided by zoom so they stay
  // a constant on-screen size (don't balloon when you zoom in)
  const hs = $derived(1 / zoom);

  // during playback, the segment under the playhead is "active"; the rest fade
  const activeSeg = $derived(sandbox.playing ? segmentAt(sandbox.time) : null);
  function segOpacity(stmtIndex: number): number {
    if (sandbox.playing && activeSeg) return activeSeg.stmtIndex === stmtIndex ? 1 : 0.26;
    return 1;
  }

  /** client px → viewBox units, via the current CTM */
  function toViewPoint(clientX: number, clientY: number): { x: number; y: number } | null {
    if (!svgEl) return null;
    const m = svgEl.getScreenCTM();
    if (!m) return null;
    const p = new DOMPoint(clientX, clientY).matrixTransform(m.inverse());
    return { x: p.x, y: p.y };
  }

  function setZoom(next: number, anchorClientX?: number, anchorClientY?: number) {
    const z0 = zoom;
    const z1 = clampN(next, ZOOM_MIN, ZOOM_MAX);
    if (z1 === z0) return;
    if (anchorClientX !== undefined && anchorClientY !== undefined) {
      const a = toViewPoint(anchorClientX, anchorClientY);
      if (a) {
        // keep the point under the cursor fixed while the window rescales
        center = { sx: a.x + (center.sx - a.x) * (z0 / z1), sy: a.y + (center.sy - a.y) * (z0 / z1) };
      }
    }
    zoom = z1;
  }
  function zoomStep(mult: number) {
    setZoom(zoom * mult);
  }
  function onWheel(e: WheelEvent) {
    e.preventDefault();
    setZoom(zoom * (e.deltaY < 0 ? 1.12 : 1 / 1.12), e.clientX, e.clientY);
  }

  // --- Replay-style camera: Follow the robot + POV heading lock + zoom --------
  // While Follow is on, ease `center` toward the robot's screen position each
  // frame (frame-rate independent) so the view tracks it through playback and
  // scrubbing. POV additionally rotates the world so the robot stays upright.
  let follow = $state(false);
  let povLock = $state(false);
  let camH = $state(0); // smoothed heading (deg) for POV
  const CAM_TAU = 0.14; // s — smaller = stiffer
  const FOLLOW_ZOOM = 2.5;
  const wrapDeg = (a: number) => {
    while (a > 180) a -= 360;
    while (a < -180) a += 360;
    return a;
  };
  function toggleFollow() {
    follow = !follow;
    if (follow) {
      if (zoom < 1.5) zoom = FOLLOW_ZOOM;
      const p = sandboxPoseAt(sandbox.time);
      if (p) {
        const s = toScreen(p.x, p.y);
        center = { sx: s.sx, sy: s.sy };
        camH = p.h;
      }
    } else {
      povLock = false;
      zoom = 1;
      center = { sx: SIZE / 2, sy: SIZE / 2 };
    }
  }
  function togglePov() {
    povLock = !povLock;
    if (povLock && !follow) toggleFollow();
  }
  const worldTransform = $derived(
    follow && povLock ? `rotate(${(-camH).toFixed(3)} ${center.sx.toFixed(2)} ${center.sy.toFixed(2)})` : undefined
  );
  $effect(() => {
    if (!follow) return;
    let raf = 0;
    let last = performance.now();
    const stepCam = (now: number) => {
      const dt = Math.min((now - last) / 1000, 0.1);
      last = now;
      const p = sandboxPoseAt(sandbox.time);
      if (p) {
        const s = toScreen(p.x, p.y);
        const a = 1 - Math.exp(-dt / CAM_TAU);
        center = { sx: center.sx + (s.sx - center.sx) * a, sy: center.sy + (s.sy - center.sy) * a };
        camH = camH + wrapDeg(p.h - camH) * a;
      }
      raf = requestAnimationFrame(stepCam);
    };
    raf = requestAnimationFrame(stepCam);
    return () => cancelAnimationFrame(raf);
  });

  // Pan: drag on empty field background. A press that never moves is a
  // deselect-click instead.
  let pan: { ctm: DOMMatrix; startView: { x: number; y: number }; center0: { sx: number; sy: number }; moved: boolean } | null =
    $state(null);
  function onSvgPointerDown(e: PointerEvent) {
    if (drag) return; // a handle grabbed it (stopPropagation) — not our event
    const m = svgEl?.getScreenCTM();
    if (!m) return;
    const p = new DOMPoint(e.clientX, e.clientY).matrixTransform(m.inverse());
    pan = { ctm: m, startView: { x: p.x, y: p.y }, center0: { ...center }, moved: false };
  }
  function onPointerMoveTop(e: PointerEvent) {
    if (trajDrag) {
      onTrajDragMove(e);
      return;
    }
    if (drag) {
      onDragMove(e);
      return;
    }
    if (pan) {
      const p = new DOMPoint(e.clientX, e.clientY).matrixTransform(pan.ctm.inverse());
      const dx = p.x - pan.startView.x;
      const dy = p.y - pan.startView.y;
      if (Math.abs(dx) + Math.abs(dy) > 0.4) {
        pan.moved = true;
        follow = false; // manual pan takes over from the follow cam
      }
      center = { sx: pan.center0.sx - dx, sy: pan.center0.sy - dy };
    }
  }
  function onPointerUpTop() {
    if (trajDrag) {
      trajDrag = null;
      return;
    }
    if (drag) {
      endDrag();
      return;
    }
    if (pan) {
      if (!pan.moved) sandbox.selected = null; // background click = deselect
      pan = null;
    }
  }

  function endDrag() {
    drag = null;
  }

  // --- [TRAJECTORY] editing overlay -----------------------------------------------
  // The active trajectory is the selected [TRAJECTORY] stmt, else the followTrajectory
  // segment under the playhead. Anchor 0 is pinned to the sim's start pose for it.
  const activeTraj = $derived.by(() => {
    const { parsed, sim } = route;
    let idx: number | null = null;
    if (sandbox.selected !== null && parsed.stmts[sandbox.selected]?.kind === 'trajectory') idx = sandbox.selected;
    else {
      const seg = sandbox.time > 0 ? segmentAt(sandbox.time) : null;
      if (seg && seg.fn === 'followTrajectory') idx = seg.stmtIndex;
    }
    if (idx === null) return null;
    const seg = sim.segments.find((s) => s.stmtIndex === idx);
    if (!seg) return null;
    const stored = getTrajectory(idx);
    const anchors: Anchor[] = (stored ? stored.anchors.map((a) => ({ ...a })) : defaultTrajectory(seg.start));
    anchors[0] = { ...anchors[0], x: seg.start.x, y: seg.start.y }; // pin to the robot
    const params = stored?.params ?? { maxVel: 60, exitVel: 0, maxAccel: 60, beta: 2, zeta: 0.7 };
    const editable = sandbox.selected === idx;
    return { stmtIndex: idx, seg, anchors, params, editable };
  });

  // baked spline for the active trajectory (speed-coloured dots + insert hit-test)
  const trajPlan = $derived.by(() => {
    if (!activeTraj) return null;
    const { anchors, params } = activeTraj;
    return bakeTrajectory(
      anchors,
      { maxVel: params.maxVel, maxAccel: params.maxAccel, startVel: 0, exitVel: params.exitVel },
      sandbox.knobs.trackwidth
    );
  });

  // tangent handle endpoints for each anchor (field coords)
  const trajHandles = $derived.by(() => {
    if (!activeTraj) return [];
    return activeTraj.anchors.map((a, i) => {
      const d = splineDir(a.ang);
      const out = i < activeTraj.anchors.length - 1 ? { x: a.x + d.x * a.out, y: a.y + d.y * a.out } : null;
      const inn = i > 0 ? { x: a.x - d.x * a.in, y: a.y - d.y * a.in } : null;
      return { i, a, out, inn };
    });
  });

  type TrajDrag = { stmtIndex: number; kind: 'anchor'; ai: number } | { stmtIndex: number; kind: 'handle'; ai: number; side: 'out' | 'in' };
  let trajDrag: TrajDrag | null = $state(null);

  function startTrajAnchorDrag(e: PointerEvent, ai: number) {
    if (!activeTraj) return;
    e.stopPropagation();
    e.preventDefault();
    ensureTrajectory(activeTraj.stmtIndex, activeTraj.seg.start);
    sandbox.selected = activeTraj.stmtIndex;
    sandbox.playing = false;
    if (ai !== 0) trajDrag = { stmtIndex: activeTraj.stmtIndex, kind: 'anchor', ai };
  }
  function startTrajHandleDrag(e: PointerEvent, ai: number, side: 'out' | 'in') {
    if (!activeTraj) return;
    e.stopPropagation();
    e.preventDefault();
    ensureTrajectory(activeTraj.stmtIndex, activeTraj.seg.start);
    sandbox.selected = activeTraj.stmtIndex;
    sandbox.playing = false;
    trajDrag = { stmtIndex: activeTraj.stmtIndex, kind: 'handle', ai, side };
  }
  function onTrajDragMove(e: PointerEvent) {
    if (!trajDrag) return;
    const f = toFieldCoords(e);
    if (!f) return;
    if (trajDrag.kind === 'anchor') moveTrajAnchor(trajDrag.stmtIndex, trajDrag.ai, f.x, f.y);
    else moveTrajHandle(trajDrag.stmtIndex, trajDrag.ai, trajDrag.side, f.x, f.y);
  }
  function onTrajDblClick(e: MouseEvent) {
    if (!activeTraj || !trajPlan) return;
    const m = svgEl?.getScreenCTM();
    if (!m) return;
    const p = new DOMPoint(e.clientX, e.clientY).matrixTransform(m.inverse());
    const fx = p.x - SIZE / 2;
    const fy = SIZE / 2 - p.y;
    // nearest dense sample → insert if close to the curve
    let best = -1;
    let bd = Infinity;
    for (let i = 0; i < trajPlan.dense.length; i++) {
      const d = Math.hypot(trajPlan.dense[i].x - fx, trajPlan.dense[i].y - fy);
      if (d < bd) {
        bd = d;
        best = i;
      }
    }
    if (best >= 0 && bd < 4 / zoom + 2) {
      ensureTrajectory(activeTraj.stmtIndex, activeTraj.seg.start);
      const s = trajPlan.dense[best];
      insertTrajAnchor(activeTraj.stmtIndex, s.seg, s.t);
    }
  }

  // --- rendering helpers ----------------------------------------------------------

  // Trajectory polyline — same design as replay's Path.svelte: full-resolution
  // odometry points, joined start→end. Prepending the segment's start pose
  // closes the tiny gap to the previous segment (they share that point).
  function segPoints(seg: { start: { x: number; y: number }; ticks: { x: number; y: number }[] }): string {
    const pts = [seg.start, ...seg.ticks];
    return pts.map((p) => { const s = toScreen(p.x, p.y); return `${s.sx},${s.sy}`; }).join(' ');
  }

  function polyPoints(pts: { x: number; y: number }[]): string {
    return pts.map((p) => { const s = toScreen(p.x, p.y); return `${s.sx},${s.sy}`; }).join(' ');
  }

  const selected = $derived(sandbox.selected);
  const selBoom = $derived.by(() => {
    if (selected === null) return null;
    const seg = route.sim.segments.find((s) => s.stmtIndex === selected);
    return seg?.boom ?? null;
  });
  // segment under the playhead (whether playing or scrubbed) — drives the live
  // boomerang lead visualization while the robot runs a boomerang.
  const playheadSeg = $derived(sandbox.time > 0 ? segmentAt(sandbox.time) : null);
  // static lead geometry (axis + estimate tracks) shows when the boom is
  // selected for editing OR the playhead is inside it
  const activeBoom = $derived(playheadSeg?.boom ?? null);
  const showBoom = $derived(selBoom ?? activeBoom);
  // LIVE boomerang geometry at the playhead — the exact replay BoomerangOverlay
  // (staging lines, retracting carrot, trailing ghost, exit plane, turn arc),
  // fed from the sim's per-tick carrot/ghost/stage data via the override prop.
  const boomOverride = $derived.by(() => {
    const seg = playheadSeg;
    if (!seg?.boom || !seg.ticks.length) return null;
    let idx = 0;
    for (let i = 0; i < seg.ticks.length; i++) {
      if (seg.ticks[i].t <= sandbox.time) idx = i;
      else break;
    }
    const k = seg.ticks[idx];
    if (k.carrotX === undefined || k.ghostX === undefined) return null;
    const prev = seg.ticks[Math.max(0, idx - 1)];
    const b = seg.boom;
    return {
      end: b.target,
      targetH: b.heading,
      perpLine: b.perpLine,
      backward: b.backward,
      init: b.initCarrot,
      carrot: { x: k.carrotX, y: k.carrotY! },
      ghost: { x: k.ghostX, y: k.ghostY! },
      robot: { x: k.x, y: k.y, h: k.h },
      prevH: prev.h,
      stage: k.stage ?? 0,
      arcR: k.arcR ?? 1e4
    };
  });

  // Replay-identical per-segment overlays: Waypoint (target dot + exit-condition
  // halos) for target movements, TurnOverlay for turns, hdd stage circles.
  interface WpInfo {
    seg: SimSegment;
    x: number;
    y: number;
    settleR: number; // settle activation circle
    crossD: number; // ec.d — ⊥ trip line, slid this far back toward us (-1 = cross off)
    backward: boolean; // reverse move — the line slides the other way
    heading: number; // approach heading for the ⊥ line
    stageR: number; // headingDisableDist circle (moveTo / boomerangTo)
  }
  const wpSegs = $derived.by((): WpInfo[] => {
    const out: WpInfo[] = [];
    const { parsed, sim } = route;
    const k = sandbox.knobs;
    for (const seg of sim.segments) {
      const s = parsed.stmts[seg.stmtIndex];
      if (!s || s.kind !== 'call') continue;
      let x: number | null = null;
      let y: number | null = null;
      let ecArg;
      if (s.fn === 'moveTo') {
        x = s.args[0]?.kind === 'num' ? s.args[0].lit.value : null;
        y = s.args[1]?.kind === 'num' ? s.args[1].lit.value : null;
        ecArg = s.args[2];
      } else if (s.fn === 'boomerangTo' && seg.boom) {
        x = seg.boom.target.x;
        y = seg.boom.target.y;
        ecArg = s.args[1];
      } else {
        continue;
      }
      if (x === null || y === null) continue;
      // omitted fields fall back to the left-panel defaults
      let settleR = k.ecLatGate;
      let crossD = k.ecLatD;
      if (ecArg?.kind === 'struct') {
        const f = ecArg.init.fields;
        if (f.settleGate && typeof f.settleGate.value === 'number') settleR = f.settleGate.value;
        if (f.d && typeof f.d.value === 'number') crossD = f.d.value;
      }
      out.push({
        seg,
        x,
        y,
        settleR,
        crossD,
        backward: seg.backward,
        heading: seg.end.h,
        stageR: s.fn === 'moveTo' || s.fn === 'boomerangTo' ? knobs_hdd() : 0
      });
    }
    return out;
  });
  function knobs_hdd(): number {
    return sandbox.knobs.headingDisableDist;
  }
</script>

<svelte:window onpointermove={onPointerMoveTop} onpointerup={onPointerUpTop} />

<section class="panel field">
  <div class="panel-label">Field — sandbox</div>
  {#if route.sim.error}
    <div class="sim-error">{route.sim.error}</div>
  {/if}

  <div class="canvas">
    <!-- svelte-ignore a11y_click_events_have_key_events a11y_no_noninteractive_element_interactions -->
    <svg
      bind:this={svgEl}
      class="field-svg"
      viewBox="{view.x} {view.y} {view.w} {view.h}"
      preserveAspectRatio="xMidYMid meet"
      class:panning={pan?.moved}
      onpointerdown={onSvgPointerDown}
      onpointermove={onHoverMove}
      onpointerleave={() => (hoverCoord = null)}
      ondblclick={onTrajDblClick}
      onwheel={onWheel}
      role="application"
      aria-label="Sandbox field"
    >
      <!-- Clip to the field: an SVG viewport clips to the ELEMENT box, not the
           viewBox, and `meet` letterboxes the field inside a taller svg — without
           this, anything off-field paints into the empty strip above/below it. -->
      <defs>
        <clipPath id="sb-field-clip">
          <rect x={fieldRect.x} y={fieldRect.y} width={fieldRect.w} height={fieldRect.h} />
        </clipPath>
      </defs>
      <g clip-path="url(#sb-field-clip)">
      <!-- POV lock counter-rotates the whole scene about the camera -->
      <g transform={worldTransform}>
      <image href={fieldImg} x={fieldRect.x} y={fieldRect.y} width={fieldRect.w} height={fieldRect.h} preserveAspectRatio="none" />

      <!-- simulated trajectory, per movement — replay Path.svelte design -->
      {#each route.sim.segments as seg (seg.stmtIndex)}
        {#if seg.ticks.length > 0}
          <polyline
            points={segPoints(seg)}
            class="traj"
            class:sel={selected === seg.stmtIndex}
            style="stroke: {movementColor(seg.fn)}; opacity: {segOpacity(seg.stmtIndex)}"
            fill="none"
            stroke-width="1.4"
            stroke-linejoin="round"
            stroke-linecap="round"
            vector-effect="non-scaling-stroke"
            onclick={(e) => {
              e.stopPropagation();
              sandbox.selected = seg.stmtIndex;
            }}
            role="presentation"
          />
        {/if}
      {/each}

      <!-- turn overlays — the exact replay TurnOverlay, fed the sim's ticks -->
      {#each route.sim.segments as seg (seg.stmtIndex + ':turn')}
        {#if isTurn(seg.fn) && seg.ticks.length > 1}
          <g opacity={segOpacity(seg.stmtIndex)}>
            <TurnOverlay
              movement={asMovement(seg)}
              trackwidth={sandbox.knobs.trackwidth}
              ticksOverride={seg.ticks}
              boundsOverride={{ start: seg.start, end: seg.end }}
            />
          </g>
        {/if}
      {/each}

      <!-- waypoints — the exact replay Waypoint (dot + exit-condition halos),
           plus the hdd stage circle, styled as in the replay field -->
      {#each wpSegs as w (w.seg.stmtIndex + ':wp')}
        {@const sp = toScreen(w.x, w.y)}
        <g opacity={segOpacity(w.seg.stmtIndex)}>
          {#if w.stageR > 0}
            <circle
              cx={sp.sx}
              cy={sp.sy}
              r={w.stageR}
              fill="none"
              stroke={movementColor(w.seg.fn)}
              stroke-width="1"
              stroke-dasharray="2 2"
              opacity="0.55"
              vector-effect="non-scaling-stroke"
            />
          {/if}
          <Waypoint x={w.x} y={w.y} settleR={w.settleR} crossD={w.crossD} backward={w.backward} heading={w.heading} />
        </g>
      {/each}

      <!-- boomerang lead geometry for EDITING (selected, or playhead inside):
           lead axis + carrot/ghost estimate tracks -->
      {#if showBoom}
        {@const tp = toScreen(showBoom.target.x, showBoom.target.y)}
        {@const cp = toScreen(showBoom.initCarrot.x, showBoom.initCarrot.y)}
        <line x1={tp.sx} y1={tp.sy} x2={cp.sx} y2={cp.sy} class="lead-axis" vector-effect="non-scaling-stroke" />
        {#if showBoom.ghostTrack.length > 1}
          <polyline points={polyPoints(showBoom.ghostTrack)} class="ghost-track" fill="none" vector-effect="non-scaling-stroke" />
        {/if}
        {#if showBoom.carrotTrack.length > 1}
          <polyline points={polyPoints(showBoom.carrotTrack)} class="carrot-track" fill="none" vector-effect="non-scaling-stroke" />
        {/if}
      {/if}

      <!-- LIVE boomerang at the playhead — the exact replay overlay: staging
           lines (ghost/carrot/end, active stage highlighted), retracting carrot,
           trailing ghost, initial-carrot anchor, exit plane, turn arc -->
      {#if boomOverride}
        <BoomerangOverlay override={boomOverride} />
      {/if}

      <!-- robot at the playhead -->
      {#if pose}
        <Robot x={pose.x} y={pose.y} h={pose.h} width={holesToInches(settings.robotWidthHoles)} length={holesToInches(settings.robotLengthHoles)} />
      {/if}

      <!-- drag layer: invisible hit circles on the movement tips; the visible
           marks are the replay waypoint dots above. Secondary handles (heading /
           carrot / gLead) appear only for the selected movement. -->
      {#each handles as h (h.stmtIndex + ':' + h.role)}
        {@const sp = toScreen(h.x, h.y)}
        {@const isSel = selected === h.stmtIndex}
        {@const secondary = h.role === 'heading' || h.role === 'startheading' || h.role === 'carrot' || h.role === 'glead'}
        {#if !secondary || isSel}
          {#if secondary && h.ox !== undefined && h.oy !== undefined}
            {@const op = toScreen(h.ox, h.oy)}
            <line x1={op.sx} y1={op.sy} x2={sp.sx} y2={sp.sy} class="stem sel" style="stroke: {h.color}" vector-effect="non-scaling-stroke" />
          {/if}

          {#if h.role === 'heading' || h.role === 'startheading'}
            <circle cx={sp.sx} cy={sp.sy} r={1.6 * hs} class="knob rk" style="fill: {h.color}" onpointerdown={(e) => startDrag(e, h)} role="presentation" />
          {:else if h.role === 'carrot'}
            {@const cr = 1.7 * hs}
            <rect x={sp.sx - cr} y={sp.sy - cr} width={2 * cr} height={2 * cr} rx={0.4 * hs} transform="rotate(45 {sp.sx} {sp.sy})" class="knob carrot-knob" onpointerdown={(e) => startDrag(e, h)} role="presentation" />
          {:else if h.role === 'glead'}
            <circle cx={sp.sx} cy={sp.sy} r={1.3 * hs} class="knob glead-knob" onpointerdown={(e) => startDrag(e, h)} role="presentation" />
          {:else}
            <!-- invisible drag hit for a movement tip (target / distance / start) -->
            {#if isSel}
              <circle cx={sp.sx} cy={sp.sy} r={3.4 * hs} class="halo" style="stroke: {h.color}" vector-effect="non-scaling-stroke" />
            {/if}
            <circle
              cx={sp.sx}
              cy={sp.sy}
              r={4 * hs}
              class="hit"
              onpointerdown={(e) => startDrag(e, h)}
              role="presentation"
            />
          {/if}
        {/if}
      {/each}

      <!-- [TRAJECTORY] editing overlay: speed-coloured spline + anchors + handles -->
      {#if activeTraj && trajPlan}
        {#each trajPlan.points as p, i}
          {#if i > 0}
            {@const a = toScreen(trajPlan.points[i - 1].x, trajPlan.points[i - 1].y)}
            {@const b = toScreen(p.x, p.y)}
            <line x1={a.sx} y1={a.sy} x2={b.sx} y2={b.sy} stroke={speedColor(p.v, activeTraj.params.maxVel)} stroke-width={2.4} stroke-linecap="round" opacity="0.92" />
          {/if}
        {/each}
        {#if activeTraj.editable}
          {#each trajHandles as hb (hb.i)}
            {@const ap = toScreen(hb.a.x, hb.a.y)}
            {@const op = hb.out ? toScreen(hb.out.x, hb.out.y) : ap}
            {@const ip = hb.inn ? toScreen(hb.inn.x, hb.inn.y) : ap}
            <line x1={ip.sx} y1={ip.sy} x2={op.sx} y2={op.sy} class="tj-stem" vector-effect="non-scaling-stroke" />
            {#if hb.out}
              <circle cx={op.sx} cy={op.sy} r={1.4 * hs} class="tj-handle" onpointerdown={(e) => startTrajHandleDrag(e, hb.i, 'out')} role="presentation" />
            {/if}
            {#if hb.inn}
              <circle cx={ip.sx} cy={ip.sy} r={1.4 * hs} class="tj-handle" onpointerdown={(e) => startTrajHandleDrag(e, hb.i, 'in')} role="presentation" />
            {/if}
          {/each}
          {#each activeTraj.anchors as a, ai (ai)}
            {@const sp = toScreen(a.x, a.y)}
            <circle cx={sp.sx} cy={sp.sy} r={2 * hs} class="tj-anchor" class:locked={ai === 0} onpointerdown={(e) => startTrajAnchorDrag(e, ai)} role="presentation" />
          {/each}
        {/if}
      {/if}
      </g>
      </g>
    </svg>
  </div>

  {#if hoverCoord}
    <div class="coord">
      <span class="cx">{hoverCoord.x.toFixed(1)}</span><span class="csep">,</span><span class="cy">{hoverCoord.y.toFixed(1)}</span>
    </div>
  {/if}

  <!-- follow cam: track the robot + POV heading lock + zoom, same as Replay -->
  <div class="cam" class:on={follow}>
    <button
      class="cam-toggle"
      class:active={follow}
      onclick={toggleFollow}
      title={follow ? 'Following robot — click for full field' : 'Follow the robot: zoom in and track it'}
    >
      <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round">
        <circle cx="8" cy="8" r="4.3" />
        <path d="M8 0.8V3.2 M8 12.8v2.4 M0.8 8H3.2 M12.8 8h2.4" />
      </svg>
      <span>Follow</span>
    </button>
    <button
      class="cam-toggle"
      class:active={povLock}
      onclick={togglePov}
      title={povLock ? 'POV locked — the field rotates around the robot' : 'Lock camera to the robot’s heading — robot POV'}
    >
      <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round">
        <path d="M8 1.5 L10.2 6 L8 5 L5.8 6 Z" fill="currentColor" stroke="none" />
        <path d="M3.2 11.5 A5.6 5.6 0 0 0 12.8 11.5" />
        <path d="M12.8 11.5 l0.3 -1.8 M12.8 11.5 l-1.8 -0.3" />
      </svg>
      <span>POV</span>
    </button>
    <div class="cam-zoom">
      <button class="z" onclick={() => zoomStep(1 / 1.3)} disabled={zoom <= ZOOM_MIN} aria-label="Zoom out">−</button>
      <span class="zlvl">{zoom.toFixed(1)}×</span>
      <button class="z" onclick={() => zoomStep(1.3)} disabled={zoom >= ZOOM_MAX} aria-label="Zoom in">+</button>
    </div>
  </div>
</section>

<style>
  .field {
    flex: 1;
    min-height: 0;
  }

  .canvas {
    position: absolute;
    inset: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 32px 16px 16px;
  }

  /* the field canvas only — a bare `svg` selector would also hit the camera-bar
     icons and stretch them to 100% */
  .field-svg {
    width: 100%;
    height: 100%;
    touch-action: none;
  }
  .field-svg.panning {
    cursor: grabbing;
  }

  image {
    border-radius: 6px;
  }

  .sim-error {
    position: absolute;
    top: 8px;
    right: 12px;
    z-index: 2;
    font-size: 9.5px;
    color: #f0a3a3;
    background: var(--bg-raised);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 4px 9px;
  }

  /* replay Path.svelte look: solid, full-opacity, 1.4 non-scaling stroke */
  .traj {
    cursor: pointer;
  }
  .traj.sel {
    stroke-width: 2.4;
  }

  .lead-axis {
    stroke: #d9b64a;
    stroke-width: 1;
    stroke-dasharray: 3 3;
    opacity: 0.8;
  }
  .ghost-track {
    stroke: #b0b0c8;
    stroke-width: 1;
    stroke-dasharray: 4 3;
    opacity: 0.55;
  }
  .carrot-track {
    stroke: #d9b64a;
    stroke-width: 1;
    stroke-dasharray: 1.5 2.5;
    opacity: 0.6;
  }

  /* SVG drag-handle glyphs — radii scale ÷zoom (constant screen size), strokes
     stay a fixed pixel width via non-scaling-stroke */
  /* [TRAJECTORY] editing overlay */
  .tj-stem {
    stroke: rgba(251, 191, 36, 0.55);
    stroke-width: 1;
    pointer-events: none;
  }
  .tj-handle {
    fill: rgba(16, 16, 16, 0.85);
    stroke: #fbbf24;
    stroke-width: 0.5;
    cursor: grab;
  }
  .tj-handle:hover {
    fill: #fbbf24;
  }
  .tj-anchor {
    fill: #f4f4f4;
    stroke: rgba(10, 10, 10, 0.75);
    stroke-width: 1.3;
    cursor: grab;
  }
  .tj-anchor:hover {
    fill: #fff;
  }
  .tj-anchor.locked {
    fill: #fbbf24;
    cursor: not-allowed;
  }

  .knob {
    cursor: grab;
    vector-effect: non-scaling-stroke;
    transition: opacity 0.12s ease;
  }
  .knob:active {
    cursor: grabbing;
  }
  /* round heading knob: colored fill (set inline) + white rim */
  .knob.rk {
    stroke: #ffffff;
    stroke-width: 1.2;
  }
  .carrot-knob {
    stroke: rgba(0, 0, 0, 0.5);
    stroke-width: 1;
  }
  .glead-knob {
    fill: #b0b0c8;
    stroke: rgba(0, 0, 0, 0.4);
    stroke-width: 0.8;
  }
  /* draggable movement TIP: invisible hit target + on-demand marker */
  .hit {
    fill: transparent;
    cursor: grab;
  }
  .hit:active {
    cursor: grabbing;
  }
        /* selection halo ring around a target */
  .halo {
    fill: none;
    opacity: 0.5;
    stroke-width: 1.4;
    pointer-events: none;
  }
  /* connector stem from a pivot to its heading / lead handle */
  .stem {
    opacity: 0.4;
    stroke-width: 1.1;
    pointer-events: none;
  }
  .stem.sel {
    opacity: 0.7;
  }


  /* live cursor coordinate readout (top-right of the field) */
  .coord {
    position: absolute;
    top: 8px;
    right: 12px;
    z-index: 3;
    display: flex;
    align-items: baseline;
    gap: 1px;
    background: var(--bg-raised);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 3px 8px;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 10px;
    font-weight: 600;
    font-variant-numeric: tabular-nums;
    pointer-events: none;
  }
  .coord .cx {
    color: #9cdcfe;
  }
  .coord .cy {
    color: #b5cea8;
  }
  .coord .csep {
    color: var(--text-ghost);
    margin-right: 3px;
  }

  .cam {
    position: absolute;
    bottom: 10px;
    right: 12px;
    z-index: 3;
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 4px 5px;
    border-radius: 10px;
    background: rgba(18, 18, 18, 0.78);
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.06), 0 2px 10px rgba(0, 0, 0, 0.4);
  }
  .cam-toggle {
    display: flex;
    align-items: center;
    gap: 5px;
    background: var(--bg-raised);
    border: none;
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 4px 9px;
    color: var(--text-dim);
    font-family: inherit;
    font-size: 10px;
    font-weight: 600;
    cursor: pointer;
    transition: color 0.15s ease, background 0.15s ease;
  }
  .cam-toggle svg {
    width: 12px;
    height: 12px;
  }
  .cam-toggle:hover {
    color: var(--text-primary);
  }
  .cam-toggle.active {
    background: rgba(255, 255, 255, 0.12);
    color: var(--text-emphasis);
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.18);
  }
  .cam-zoom {
    display: flex;
    align-items: center;
    gap: 4px;
    background: var(--bg-raised);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 2px 4px;
  }
  .z {
    background: none;
    border: none;
    color: var(--text-secondary);
    font-family: inherit;
    font-size: 14px;
    line-height: 1;
    width: 18px;
    height: 18px;
    cursor: pointer;
    border-radius: 4px;
    transition: color 0.15s ease, background 0.15s ease;
  }
  .z:hover:not(:disabled) {
    color: var(--text-primary);
    background: rgba(255, 255, 255, 0.08);
  }
  .z:disabled {
    opacity: 0.35;
    cursor: default;
  }
  .zlvl {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 10px;
    font-weight: 600;
    font-variant-numeric: tabular-nums;
    color: var(--text-secondary);
    min-width: 30px;
    text-align: center;
    padding: 0;
  }
  .zlvl:hover {
    color: var(--text-primary);
  }
</style>
