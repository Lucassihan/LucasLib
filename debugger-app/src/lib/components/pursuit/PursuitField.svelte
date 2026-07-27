<script lang="ts">
  // Pure Pursuit planner field — a bezier spline you shape directly.
  // Drag anchors to move them, drag their tangent handles to steer the curve.
  // Double-click ON the path to insert an anchor there (the shape doesn't
  // change); double-click empty field to extend the path. The dots along the
  // curve are the ACTUAL baked PathPoints, colored by their planned speed.
  import { toScreen } from '$lib/field/coords';
  import { backdropSrc, fieldImageRect } from '$lib/field/backdrops';
  import { settings, holesToInches } from '$lib/stores/settings.svelte';
  import { base } from '$app/paths';
  import Robot from '../Robot.svelte';
  import {
    pursuit,
    viewOpts,
    playback,
    getPlan,
    speedColor,
    speedRange,
    moveAnchor,
    moveHandle,
    appendAnchor,
    insertAnchor,
    deleteAnchor,
    nudgeSelected,
    snapCoord,
    pushHistory,
    undo,
    redo,
    type Pt
  } from '$lib/stores/pursuit.svelte';

  const SIZE = 144;
  const fieldRect = fieldImageRect(SIZE);
  const fieldImg = $derived(backdropSrc(settings.fieldBackdrop, base));

  const plan = $derived(getPlan());
  const D2R = Math.PI / 180;

  function scr(p: Pt) {
    return toScreen(p.x, p.y);
  }

  /** Format a planned speed in the chosen readout unit (in/s · ft/s · % of max). */
  function fmtSpeed(v: number): string {
    if (viewOpts.unit === 'ft/s') return (v / 12).toFixed(1) + ' ft/s';
    if (viewOpts.unit === '%') return Math.round((v / Math.max(1, pursuit.params.maxVel)) * 100) + '%';
    return v.toFixed(0) + ' in/s';
  }

  // The visible line is drawn straight from the DENSE spline samples (which now
  // carry their own arc length `.s` and planned speed `.v`), decoupled from the
  // coarse export spacing — so the curve is smooth AND the speed colour is
  // full-resolution (every dense vertex, not the 2" export points). Colour spans
  // the path's own [min,max] speed. Drawn twice: a blurred glow under a bright core.
  const RENDER_STEP = 0.6; // inches between rendered vertices
  const glowSegs = $derived.by(() => {
    const out: { x1: number; y1: number; x2: number; y2: number; color: string }[] = [];
    const dense = plan.dense;
    if (dense.length < 2) return out;
    const { lo, hi } = speedRange(dense, pursuit.params.maxVel);
    let prev = scr(dense[0]);
    let prevS = dense[0].s;
    let prevV = dense[0].v;
    for (let i = 1; i < dense.length; i++) {
      if (dense[i].s - prevS < RENDER_STEP && i < dense.length - 1) continue;
      const b = scr(dense[i]);
      out.push({
        x1: prev.sx,
        y1: prev.sy,
        x2: b.sx,
        y2: b.sy,
        color: speedColor((prevV + dense[i].v) / 2, lo, hi)
      });
      prev = b;
      prevS = dense[i].s;
      prevV = dense[i].v;
    }
    return out;
  });

  // tangent handles for EVERY anchor (always visible — the spline is the UI).
  // Collinear handles mean one straight bar through the anchor, dot at each end.
  const handleBars = $derived(
    pursuit.anchors.map((a, i) => {
      const d = { x: Math.sin(a.ang * D2R), y: Math.cos(a.ang * D2R) };
      const out = i < pursuit.anchors.length - 1 ? { x: a.x + d.x * a.out, y: a.y + d.y * a.out } : null;
      const inn = i > 0 ? { x: a.x - d.x * a.in, y: a.y - d.y * a.in } : null;
      return { i, a, out, inn };
    })
  );

  // optional snap grid — lines every viewOpts.snap inches, drawn in the same
  // field→screen space as the anchors so they line up exactly.
  const gridLines = $derived.by(() => {
    const g = viewOpts.snap;
    if (!g) return [] as { x1: number; y1: number; x2: number; y2: number }[];
    const half = SIZE / 2;
    const lines: { x1: number; y1: number; x2: number; y2: number }[] = [];
    for (let x = -half; x <= half + 1e-6; x += g) {
      const a = scr({ x, y: -half });
      const b = scr({ x, y: half });
      lines.push({ x1: a.sx, y1: a.sy, x2: b.sx, y2: b.sy });
    }
    for (let y = -half; y <= half + 1e-6; y += g) {
      const a = scr({ x: -half, y });
      const b = scr({ x: half, y });
      lines.push({ x1: a.sx, y1: a.sy, x2: b.sx, y2: b.sy });
    }
    return lines;
  });

  // --- zoom + pan (same window-into-the-field model as the sandbox field) -----
  let svgEl: SVGSVGElement | undefined = $state();
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

  const hs = $derived(1 / zoom); // keep glyphs a constant on-screen size

  function toViewPoint(clientX: number, clientY: number): { x: number; y: number } | null {
    if (!svgEl) return null;
    const m = svgEl.getScreenCTM();
    if (!m) return null;
    const p = new DOMPoint(clientX, clientY).matrixTransform(m.inverse());
    return { x: p.x, y: p.y };
  }
  function setZoom(next: number, ax?: number, ay?: number) {
    const z0 = zoom;
    const z1 = clampN(next, ZOOM_MIN, ZOOM_MAX);
    if (z1 === z0) return;
    if (ax !== undefined && ay !== undefined) {
      const a = toViewPoint(ax, ay);
      if (a) center = { sx: a.x + (center.sx - a.x) * (z0 / z1), sy: a.y + (center.sy - a.y) * (z0 / z1) };
    }
    zoom = z1;
  }
  function onWheel(e: WheelEvent) {
    e.preventDefault();
    setZoom(zoom * (e.deltaY < 0 ? 1.12 : 1 / 1.12), e.clientX, e.clientY);
  }

  // --- pointer plumbing -------------------------------------------------------
  function toFieldCoords(clientX: number, clientY: number): Pt | null {
    if (!svgEl) return null;
    const m = svgEl.getScreenCTM();
    if (!m) return null;
    const p = new DOMPoint(clientX, clientY).matrixTransform(m.inverse());
    return { x: p.x - SIZE / 2, y: SIZE / 2 - p.y };
  }

  let hoverCoord = $state<Pt | null>(null);

  type Drag = { kind: 'anchor'; i: number } | { kind: 'handle'; i: number; side: 'out' | 'in' };
  let drag: Drag | null = $state(null);
  let dragDirty = false; // has this gesture committed a history snapshot yet?

  function startAnchorDrag(e: PointerEvent, i: number) {
    e.stopPropagation();
    e.preventDefault();
    drag = { kind: 'anchor', i };
    dragDirty = false;
    pursuit.selected = i;
  }
  function startHandleDrag(e: PointerEvent, i: number, side: 'out' | 'in') {
    e.stopPropagation();
    e.preventDefault();
    drag = { kind: 'handle', i, side };
    dragDirty = false;
    pursuit.selected = i;
  }

  // live readout of whatever is being dragged (floating chip)
  const dragInfo = $derived.by(() => {
    if (!drag) return null;
    const a = pursuit.anchors[drag.i];
    if (!a) return null;
    if (drag.kind === 'anchor')
      return [
        ['X', a.x.toFixed(1)],
        ['Y', a.y.toFixed(1)],
        ['DIR', Math.round(a.ang) + '°']
      ];
    return [
      ['LEN', (drag.side === 'out' ? a.out : a.in).toFixed(1) + '"'],
      ['ANG', Math.round(a.ang) + '°']
    ];
  });

  let pan: { ctm: DOMMatrix; startView: Pt; center0: { sx: number; sy: number }; moved: boolean } | null = $state(null);
  function onSvgPointerDown(e: PointerEvent) {
    if (drag) return;
    const m = svgEl?.getScreenCTM();
    if (!m) return;
    const p = new DOMPoint(e.clientX, e.clientY).matrixTransform(m.inverse());
    pan = { ctm: m, startView: { x: p.x, y: p.y }, center0: { ...center }, moved: false };
  }
  function onPointerMoveTop(e: PointerEvent) {
    const f = toFieldCoords(e.clientX, e.clientY);
    hoverCoord = f ? { x: Math.round(f.x * 10) / 10, y: Math.round(f.y * 10) / 10 } : null;
    if (drag) {
      if (!f) return;
      if (!dragDirty) {
        pushHistory('drag:' + drag.kind);
        dragDirty = true;
      }
      if (drag.kind === 'anchor') {
        let x = f.x;
        let y = f.y;
        // grid snap (Alt bypasses it for a free move)
        if (viewOpts.snap > 0 && !e.altKey) {
          x = snapCoord(x, viewOpts.snap);
          y = snapCoord(y, viewOpts.snap);
        }
        moveAnchor(drag.i, x, y);
      } else {
        let x = f.x;
        let y = f.y;
        // Shift snaps the tangent DIRECTION to 15° increments
        if (e.shiftKey) {
          const a = pursuit.anchors[drag.i];
          const dx = x - a.x;
          const dy = y - a.y;
          const len = Math.hypot(dx, dy);
          const snapped = (Math.round((Math.atan2(dx, dy) / D2R) / 15) * 15) * D2R;
          x = a.x + Math.sin(snapped) * len;
          y = a.y + Math.cos(snapped) * len;
        }
        moveHandle(drag.i, drag.side, x, y);
      }
      return;
    }
    if (pan) {
      const p = new DOMPoint(e.clientX, e.clientY).matrixTransform(pan.ctm.inverse());
      const dx = p.x - pan.startView.x;
      const dy = p.y - pan.startView.y;
      if (Math.abs(dx) + Math.abs(dy) > 0.4) {
        pan.moved = true;
        follow = false; // manual pan drops the follow camera
      }
      center = { sx: pan.center0.sx - dx, sy: pan.center0.sy - dy };
    }
  }
  function onPointerUpTop() {
    if (drag) {
      drag = null;
      return;
    }
    if (pan) {
      if (!pan.moved) pursuit.selected = null; // background click = deselect
      pan = null;
    }
  }

  // Double-click ON the curve inserts an anchor there (shape-preserving split);
  // anywhere else appends to the end of the path.
  function onDblClick(e: MouseEvent) {
    const f = toFieldCoords(e.clientX, e.clientY);
    if (!f) return;
    let best = -1;
    let bd = Infinity;
    for (let i = 0; i < plan.dense.length; i++) {
      const d = Math.hypot(plan.dense[i].x - f.x, plan.dense[i].y - f.y);
      if (d < bd) {
        bd = d;
        best = i;
      }
    }
    if (best >= 0 && bd < 4 / zoom + 2) {
      const s = plan.dense[best];
      insertAnchor(s.seg, s.t);
    } else {
      appendAnchor(f.x, f.y);
    }
  }

  const anchorsScreen = $derived(pursuit.anchors.map((a, i) => ({ ...scr(a), a, i })));

  // --- playback: watch the robot follow the baked path ------------------------
  const R2D = 180 / Math.PI;

  // time-parameterize the baked points: dt = ds / average speed (floored so the
  // near-zero speed at the very end can't blow the clock up to infinity).
  const timed = $derived.by(() => {
    const pts = plan.points;
    const times = [0];
    for (let i = 1; i < pts.length; i++) {
      const ds = pts[i].s - pts[i - 1].s;
      const vAvg = Math.max(2, (pts[i].v + pts[i - 1].v) / 2);
      times.push(times[i - 1] + ds / vAvg);
    }
    return { times, duration: times[times.length - 1] ?? 0 };
  });

  // keep the playhead valid when the path re-bakes under it
  $effect(() => {
    if (playback.t > timed.duration) playback.t = timed.duration;
  });

  // full sample at the playhead: pose + arc length + planned speed + curvature,
  // interpolated along the baked points (κ pulled from the nearest dense sample)
  const atPlayhead = $derived.by(() => {
    const pts = plan.points;
    if (pts.length < 2) return null;
    const { times } = timed;
    const tt = Math.max(0, Math.min(playback.t, timed.duration));
    let i = 1;
    while (i < times.length - 1 && times[i] < tt) i++;
    const t0 = times[i - 1];
    const t1 = times[i];
    const u = t1 > t0 ? Math.max(0, Math.min(1, (tt - t0) / (t1 - t0))) : 0;
    const p0 = pts[i - 1];
    const p1 = pts[i];
    const x = p0.x + (p1.x - p0.x) * u;
    const y = p0.y + (p1.y - p0.y) * u;
    const s = p0.s + (p1.s - p0.s) * u;
    const v = p0.v + (p1.v - p0.v) * u;
    const h = Math.atan2(p1.x - p0.x, p1.y - p0.y) * R2D;
    // curvature at this arc length — binary-search the dense samples by .s
    let k = 0;
    const dense = plan.dense;
    if (dense.length) {
      let lo = 0;
      let hi = dense.length - 1;
      while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (dense[mid].s < s) lo = mid + 1;
        else hi = mid;
      }
      k = Math.abs(dense[lo].k);
    }
    return { x, y, h, s, v, k, t: tt };
  });

  // robot pose at the playhead — the start pose (with its heading) at rest,
  // else the interpolated sample
  const robotPose = $derived.by(() => {
    const a0 = pursuit.anchors[0];
    if (plan.points.length < 2 || playback.t <= 0) return a0 ? { x: a0.x, y: a0.y, h: a0.ang } : null;
    const s = atPlayhead;
    return s ? { x: s.x, y: s.y, h: s.h } : null;
  });

  // --- optional field overlays ------------------------------------------------
  const HALF = SIZE / 2;

  // off-field detection — anchors / path beyond the field half-extent
  const offField = $derived.by(() => {
    const anchors = new Set<number>();
    pursuit.anchors.forEach((a, i) => {
      if (Math.abs(a.x) > HALF || Math.abs(a.y) > HALF) anchors.add(i);
    });
    let any = anchors.size > 0;
    if (!any)
      for (const d of plan.dense)
        if (Math.abs(d.x) > HALF || Math.abs(d.y) > HALF) {
          any = true;
          break;
        }
    return { any, anchors };
  });

  // --- Replay-style camera: Follow the robot + POV heading lock + zoom ---------
  let follow = $state(false);
  let povLock = $state(false);
  let camH = $state(0); // smoothed heading (deg) for POV
  const CAM_TAU = 0.14;
  const FOLLOW_ZOOM = 2.5;
  const wrapDeg = (a: number) => {
    while (a > 180) a -= 360;
    while (a < -180) a += 360;
    return a;
  };
  $effect(() => {
    if (!follow) return;
    let raf = 0;
    let last = performance.now();
    const step = (now: number) => {
      const dt = Math.min((now - last) / 1000, 0.1);
      last = now;
      const rp = robotPose;
      if (rp) {
        const s = scr(rp);
        const a = 1 - Math.exp(-dt / CAM_TAU);
        center = { sx: center.sx + (s.sx - center.sx) * a, sy: center.sy + (s.sy - center.sy) * a };
        camH = camH + wrapDeg(rp.h - camH) * a;
      }
      raf = requestAnimationFrame(step);
    };
    raf = requestAnimationFrame(step);
    return () => cancelAnimationFrame(raf);
  });

  // POV lock: counter-rotate the whole scene about the camera so the robot is upright
  const worldTransform = $derived(
    follow && povLock ? `rotate(${(-camH).toFixed(3)} ${center.sx.toFixed(2)} ${center.sy.toFixed(2)})` : undefined
  );

  function toggleFollow() {
    follow = !follow;
    if (follow) {
      if (zoom < 1.5) zoom = FOLLOW_ZOOM;
      const rp = robotPose;
      if (rp) {
        const s = scr(rp);
        center = { sx: s.sx, sy: s.sy };
        camH = rp.h;
      }
    } else {
      // back to the full field for editing (same as Replay's follow-off)
      povLock = false;
      zoom = 1;
      center = { sx: SIZE / 2, sy: SIZE / 2 };
    }
  }
  function togglePov() {
    povLock = !povLock;
    if (povLock && !follow) toggleFollow();
  }
  function zoomBy(d: number) {
    setZoom(zoom + d);
  }

  let raf = 0;
  let lastTs = 0;
  const SPEEDS = [0.25, 0.5, 1, 2, 4];
  function cycleSpeed() {
    const idx = SPEEDS.indexOf(viewOpts.speed);
    viewOpts.speed = SPEEDS[(idx + 1) % SPEEDS.length] ?? 1;
  }
  function frame(ts: number) {
    if (!playback.playing) return;
    if (!lastTs) lastTs = ts;
    playback.t = playback.t + ((ts - lastTs) / 1000) * viewOpts.speed;
    lastTs = ts;
    if (playback.t >= timed.duration) {
      if (viewOpts.loop) playback.t = 0; // seamless loop
      else {
        playback.t = timed.duration;
        playback.playing = false;
      }
    }
    if (playback.playing) raf = requestAnimationFrame(frame);
  }
  function play() {
    if (timed.duration <= 0) return;
    if (playback.t >= timed.duration) playback.t = 0; // replay from the top
    playback.playing = true;
    lastTs = 0;
    raf = requestAnimationFrame(frame);
  }
  function pause() {
    playback.playing = false;
    if (raf) cancelAnimationFrame(raf);
  }
  function togglePlay() {
    if (playback.playing) pause();
    else play();
  }
  function scrub(v: number) {
    pause();
    playback.t = Math.max(0, Math.min(timed.duration, v));
  }

  // --- timeline: velocity-coloured track ("cover overlay" = the path's speed) --
  const trackGradient = $derived.by(() => {
    const pts = plan.points;
    if (pts.length < 2 || timed.duration <= 0) return '';
    const { lo, hi } = speedRange(pts, pursuit.params.maxVel);
    const stops = pts.map((p, i) => `${speedColor(p.v, lo, hi)} ${((timed.times[i] / timed.duration) * 100).toFixed(1)}%`);
    return `linear-gradient(to right, ${stops.join(', ')})`;
  });

  const axisTicks = $derived.by(() => {
    const d = timed.duration;
    if (d <= 0) return [] as number[];
    const step = d <= 3 ? 0.5 : d <= 6 ? 1 : d <= 12 ? 2 : 5;
    const out: number[] = [];
    for (let t = 0; t <= d + 1e-6; t += step) out.push(t);
    return out;
  });

  let trackEl: HTMLElement | undefined = $state();
  let trackDragging = false;
  function trackScrub(e: PointerEvent) {
    if (!trackEl || timed.duration <= 0) return;
    const r = trackEl.getBoundingClientRect();
    const f = Math.max(0, Math.min(1, (e.clientX - r.left) / r.width));
    scrub(f * timed.duration);
  }
  function onTrackDown(e: PointerEvent) {
    trackDragging = true;
    trackEl?.setPointerCapture?.(e.pointerId);
    trackScrub(e);
  }
  function onTrackMove(e: PointerEvent) {
    if (trackDragging) trackScrub(e);
  }
  function onTrackUp() {
    trackDragging = false;
  }

  $effect(() => () => {
    if (raf) cancelAnimationFrame(raf);
  });

  // --- keyboard: camera (f/p/±) · undo/redo · nudge the selected anchor · delete
  function onKeyDown(e: KeyboardEvent) {
    const t = e.target as HTMLElement | null;
    if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable)) return;
    const meta = e.metaKey || e.ctrlKey;
    if (!meta) {
      if (e.key === 'f' || e.key === 'F') return toggleFollow();
      if (e.key === 'p' || e.key === 'P') return togglePov();
      if (e.key === '+' || e.key === '=') return zoomBy(0.5);
      if (e.key === '-' || e.key === '_') return zoomBy(-0.5);
    }
    if (meta && (e.key === 'z' || e.key === 'Z')) {
      e.preventDefault();
      if (e.shiftKey) redo();
      else undo();
      return;
    }
    if (meta && (e.key === 'y' || e.key === 'Y')) {
      e.preventDefault();
      redo();
      return;
    }
    if (pursuit.selected === null) return;
    const step = e.shiftKey ? 6 : e.altKey ? 0.1 : 1;
    switch (e.key) {
      case 'ArrowLeft':
        e.preventDefault();
        nudgeSelected(-step, 0);
        break;
      case 'ArrowRight':
        e.preventDefault();
        nudgeSelected(step, 0);
        break;
      case 'ArrowUp':
        e.preventDefault();
        nudgeSelected(0, step);
        break;
      case 'ArrowDown':
        e.preventDefault();
        nudgeSelected(0, -step);
        break;
      case 'Delete':
      case 'Backspace':
        e.preventDefault();
        deleteAnchor(pursuit.selected);
        break;
    }
  }
</script>

<svelte:window onpointermove={onPointerMoveTop} onpointerup={onPointerUpTop} onkeydown={onKeyDown} />

<section class="panel field">
  <div class="panel-label">Field — trajectory</div>
  <div class="field-wrap">
    <svg
      bind:this={svgEl}
      class="field-svg"
      viewBox="{view.x} {view.y} {view.w} {view.h}"
      preserveAspectRatio="xMidYMid meet"
      class:panning={pan?.moved}
      onpointerdown={onSvgPointerDown}
      ondblclick={onDblClick}
      onwheel={onWheel}
      role="application"
      aria-label="Trajectory spline editor"
    >
      <!-- Clip everything to the field. An SVG viewport clips to the ELEMENT box,
           not the viewBox, and `meet` letterboxes the field inside a taller svg —
           so without this, a handle whose endpoint lands off-field gets painted
           into the empty strip above/below the field and reads as a stray dot. -->
      <defs>
        <clipPath id="pf-field-clip">
          <rect x={fieldRect.x} y={fieldRect.y} width={fieldRect.w} height={fieldRect.h} />
        </clipPath>
      </defs>
      <g clip-path="url(#pf-field-clip)">
      <!-- POV lock counter-rotates the whole scene about the camera -->
      <g transform={worldTransform}>
      <image href={fieldImg} x={fieldRect.x} y={fieldRect.y} width={fieldRect.w} height={fieldRect.h} preserveAspectRatio="none" />

      {#if gridLines.length}
        <g class="grid">
          {#each gridLines as l}
            <line x1={l.x1} y1={l.y1} x2={l.x2} y2={l.y2} vector-effect="non-scaling-stroke" />
          {/each}
        </g>
      {/if}

      <!-- glow-core path: a blurred pass under a bright core, both speed-colored -->
      <defs>
        <filter id="path-glow" x="-40%" y="-40%" width="180%" height="180%">
          <feGaussianBlur stdDeviation="1.5" />
        </filter>
      </defs>
      <g class="glow" filter="url(#path-glow)">
        {#each glowSegs as s}
          <line x1={s.x1} y1={s.y1} x2={s.x2} y2={s.y2} stroke={s.color} stroke-width={3.2} stroke-linecap="round" />
        {/each}
      </g>
      <g class="core">
        {#each glowSegs as s}
          <line
            x1={s.x1}
            y1={s.y1}
            x2={s.x2}
            y2={s.y2}
            stroke={s.color}
            stroke-width={1.8}
            stroke-linecap="round"
            vector-effect="non-scaling-stroke"
          />
        {/each}
      </g>

      <!-- the robot (shared sandbox/replay drawing): the start pose at rest, or
           wherever the playhead is along the path while scrubbing/playback.playing -->
      {#if robotPose}
        <Robot
          x={robotPose.x}
          y={robotPose.y}
          h={robotPose.h}
          width={holesToInches(settings.robotWidthHoles)}
          length={holesToInches(settings.robotLengthHoles)}
        />
      {/if}

      <!-- tangent bars: always visible — the spline IS the interface. Collinear
           handles draw as one straight bar through the anchor, a dot at each end. -->
      {#each handleBars as hb}
        {@const ap = scr(hb.a)}
        {@const sel = pursuit.selected === hb.i}
        {@const op = hb.out ? scr(hb.out) : ap}
        {@const ip = hb.inn ? scr(hb.inn) : ap}
        <g class="hbar" class:sel={sel}>
          <polyline points="{ip.sx},{ip.sy} {ap.sx},{ap.sy} {op.sx},{op.sy}" class="stem" fill="none" vector-effect="non-scaling-stroke" />
          {#if hb.out}
            <g
              class="handle"
              role="button"
              tabindex="-1"
              aria-label="Anchor {hb.i + 1} outgoing tangent"
              onpointerdown={(e) => startHandleDrag(e, hb.i, 'out')}
            >
              <circle cx={op.sx} cy={op.sy} r={4 * hs} class="grab" />
              <circle cx={op.sx} cy={op.sy} r={0.85 * hs} class="dot" />
            </g>
          {/if}
          {#if hb.inn}
            <g
              class="handle"
              role="button"
              tabindex="-1"
              aria-label="Anchor {hb.i + 1} incoming tangent"
              onpointerdown={(e) => startHandleDrag(e, hb.i, 'in')}
            >
              <circle cx={ip.sx} cy={ip.sy} r={4 * hs} class="grab" />
              <circle cx={ip.sx} cy={ip.sy} r={0.85 * hs} class="dot" />
            </g>
          {/if}
        </g>
      {/each}

      <!-- anchors: target reticles. The start (anchor 0) is the robot itself,
           so it gets no reticle — just a gold marker ring when selected. -->
      {#each anchorsScreen as w}
        <g
          class="anchor"
          class:sel={pursuit.selected === w.i}
          role="button"
          tabindex="-1"
          aria-label="Anchor {w.i + 1}"
          onpointerdown={(e) => startAnchorDrag(e, w.i)}
        >
          <circle cx={w.sx} cy={w.sy} r={5 * hs} class="hit" />
          {#if offField.anchors.has(w.i)}
            <circle cx={w.sx} cy={w.sy} r={3 * hs} class="offring" vector-effect="non-scaling-stroke" />
          {/if}
          {#if w.a.vCap !== undefined}
            <circle cx={w.sx} cy={w.sy} r={2.4 * hs} class="capring" class:stop={w.a.vCap === 0} vector-effect="non-scaling-stroke" />
          {/if}
          {#if w.i === 0}
            {#if pursuit.selected === 0}
              <circle
                cx={w.sx}
                cy={w.sy}
                r={holesToInches(settings.robotWidthHoles) * 0.66}
                class="startsel"
                vector-effect="non-scaling-stroke"
              />
            {/if}
          {:else}
            <circle cx={w.sx} cy={w.sy} r={0.95 * hs} class="core" />
          {/if}
          <text x={w.sx + 6.5 * hs} y={w.sy - 5.5 * hs} font-size={3.2 * hs} class="num">{w.i + 1}</text>
        </g>
      {/each}
      </g>
      </g>
    </svg>

    {#if hoverCoord}
      <div class="coord">
        <span>{hoverCoord.x.toFixed(1)}</span><span class="csep">,</span><span>{hoverCoord.y.toFixed(1)}</span>
      </div>
    {/if}

    {#if dragInfo}
      <div class="dragchip">
        {#each dragInfo as [k, v]}
          <span class="dk">{k}</span><span class="dv">{v}</span>
        {/each}
      </div>
    {:else if offField.any}
      <div class="warn">⚠ path leaves the field</div>
    {/if}

    {#if atPlayhead && playback.t > 0}
      <div class="playread">
        <span class="pk">t</span><span class="pv">{atPlayhead.t.toFixed(2)}s</span>
        <span class="pk">s</span><span class="pv">{atPlayhead.s.toFixed(0)}″</span>
        <span class="pk">v</span><span class="pv">{fmtSpeed(atPlayhead.v)}</span>
        <span class="pk">κ</span><span class="pv">{atPlayhead.k.toFixed(3)}</span>
      </div>
    {/if}

    <div class="hint">dbl-click curve inserts · drag to shape · ⌫ delete · ⌘Z undo · ←→↑↓ nudge</div>

    <!-- follow cam: track the robot + POV heading lock + zoom, same as Replay -->
    <div class="cam" class:on={follow}>
      <button
        class="cam-toggle"
        class:active={follow}
        onclick={toggleFollow}
        title={follow ? 'Following robot — click for full field  (f)' : 'Follow the robot: zoom in and track it  (f)'}
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
        title={povLock ? 'POV locked — the field rotates around the robot  (p)' : 'Lock camera to the robot’s heading — robot POV  (p)'}
      >
        <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round">
          <path d="M8 1.5 L10.2 6 L8 5 L5.8 6 Z" fill="currentColor" stroke="none" />
          <path d="M3.2 11.5 A5.6 5.6 0 0 0 12.8 11.5" />
          <path d="M12.8 11.5 l0.3 -1.8 M12.8 11.5 l-1.8 -0.3" />
        </svg>
        <span>POV</span>
      </button>
      <div class="cam-zoom">
        <button class="z" onclick={() => zoomBy(-0.5)} disabled={zoom <= ZOOM_MIN} aria-label="Zoom out">−</button>
        <span class="zlvl">{zoom.toFixed(1)}×</span>
        <button class="z" onclick={() => zoomBy(0.5)} disabled={zoom >= ZOOM_MAX} aria-label="Zoom in">+</button>
      </div>
    </div>
  </div>

  <div class="timeline">
    <button class="play" onclick={togglePlay} disabled={timed.duration <= 0} aria-label={playback.playing ? 'Pause' : 'Play'}>
      {#if playback.playing}
        <svg viewBox="0 0 12 12"><rect x="2.5" y="2" width="2.5" height="8" /><rect x="7" y="2" width="2.5" height="8" /></svg>
      {:else}
        <svg viewBox="0 0 12 12"><path d="M3 2 L10 6 L3 10 Z" /></svg>
      {/if}
    </button>
    <button class="speed" onclick={cycleSpeed} title="Playback speed">{viewOpts.speed}×</button>
    <button class="speed" class:on={viewOpts.loop} onclick={() => (viewOpts.loop = !viewOpts.loop)} title="Loop playback" aria-label="Loop">↻</button>

    <div class="body">
      <div
        class="track"
        bind:this={trackEl}
        onpointerdown={onTrackDown}
        onpointermove={onTrackMove}
        onpointerup={onTrackUp}
        onpointerleave={onTrackUp}
        role="slider"
        tabindex="0"
        aria-label="Timeline scrubber"
        aria-valuenow={Math.round(playback.t * 100) / 100}
        aria-valuemin="0"
        aria-valuemax={Math.round(timed.duration * 100) / 100}
      >
        <div class="rail" style={trackGradient ? `background: ${trackGradient}` : ''}></div>
        <div class="handle" style="left: {timed.duration > 0 ? (playback.t / timed.duration) * 100 : 0}%"></div>
      </div>
      <div class="axis">
        {#each axisTicks as t}
          <div class="axis-tick" style="left: {(t / timed.duration) * 100}%">
            <span class="tick-mark"></span>
            <span class="tick-label">{t % 1 === 0 ? t.toFixed(0) : t.toFixed(1)}s</span>
          </div>
        {/each}
      </div>
    </div>

    <span class="clock">{playback.t.toFixed(1)} <span class="clock-sep">/</span> {timed.duration.toFixed(1)}s</span>
  </div>
</section>

<style>
  .field {
    flex: 1;
    display: flex;
    flex-direction: column;
    min-height: 0;
    padding: 10px;
  }
  .panel-label {
    position: static;
    font-size: 10px;
    font-weight: 500;
    color: var(--text-muted);
    letter-spacing: 0.01em;
    margin: 0 2px 8px;
    flex-shrink: 0;
  }
  .field-wrap {
    position: relative;
    flex: 1;
    min-height: 0;
    display: flex;
  }
  /* the field canvas only — NOT a bare `svg` selector, which would leak onto
     the icon svgs in the camera bar / timeline and stretch them (flex: 1) */
  .field-svg {
    flex: 1;
    min-height: 0;
    width: 100%;
    border-radius: var(--radius-small);
    background: var(--bg-base);
    cursor: crosshair;
    touch-action: none;
  }
  .field-svg.panning {
    cursor: grabbing;
  }
  image {
    opacity: 0.9;
  }


  /* Replay-style follow / POV / zoom camera bar */
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
    font-size: 10px;
    font-weight: 600;
    font-variant-numeric: tabular-nums;
    color: var(--text-secondary);
    min-width: 30px;
    text-align: center;
  }

  .grid line {
    stroke: rgba(255, 255, 255, 0.07);
    stroke-width: 0.75;
    pointer-events: none;
  }

  /* glow-core path: the blurred underlay carries the light, the core the line */
  .glow {
    opacity: 0.42;
    pointer-events: none;
  }
  .core {
    pointer-events: none;
  }

  .anchor {
    cursor: grab;
  }
  .anchor:active {
    cursor: grabbing;
  }
  .anchor .hit {
    fill: transparent;
  }

  /* anchor: a small solid dot (no ring, no outline) */
  .anchor .core {
    fill: #f4f4f4;
    transition: fill 0.12s ease;
  }
  .anchor:hover .core {
    fill: #fff;
  }
  .anchor.sel .core {
    fill: #fbbf24;
  }

  /* the start anchor IS the robot — a dashed gold ring marks it when selected */
  .anchor .startsel {
    fill: none;
    stroke: rgba(251, 191, 36, 0.6);
    stroke-width: 1.4;
    stroke-dasharray: 3 3;
  }
  .anchor .num {
    fill: rgba(255, 255, 255, 0.5);
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-weight: 700;
    pointer-events: none;
  }
  .anchor.sel .num {
    fill: rgba(251, 191, 36, 0.9);
  }

  /* tangent bars: quiet stem, small solid dot with a big invisible grab area */
  .hbar .stem {
    stroke: rgba(255, 255, 255, 0.22);
    stroke-width: 1;
    pointer-events: none;
    transition: stroke 0.12s ease;
  }
  .hbar .handle {
    cursor: grab;
  }
  .hbar .handle:active {
    cursor: grabbing;
  }
  .hbar .handle .grab {
    fill: transparent;
  }
  .hbar .handle .dot {
    fill: rgba(240, 240, 240, 0.95);
    transition: fill 0.12s ease;
  }
  .hbar .handle:hover .dot {
    fill: #fff;
  }
  .hbar.sel .stem {
    stroke: rgba(251, 191, 36, 0.55);
  }
  .hbar.sel .handle .dot {
    fill: #fbbf24;
  }

  .coord {
    position: absolute;
    top: 6px;
    right: 8px;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 10px;
    color: var(--text-dim);
    background: rgba(0, 0, 0, 0.45);
    padding: 2px 6px;
    border-radius: var(--radius-small);
    pointer-events: none;
    font-variant-numeric: tabular-nums;
  }
  .csep {
    opacity: 0.5;
    margin: 0 2px;
  }

  /* live readout of the anchor/handle being dragged */
  .dragchip {
    position: absolute;
    top: 6px;
    left: 50%;
    transform: translateX(-50%);
    display: flex;
    align-items: baseline;
    gap: 4px;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 10px;
    color: var(--text-primary);
    background: rgba(0, 0, 0, 0.55);
    padding: 3px 8px;
    border-radius: var(--radius-small);
    pointer-events: none;
    font-variant-numeric: tabular-nums;
    box-shadow: 0 0 0 1px rgba(251, 191, 36, 0.3);
  }
  .dragchip .dk {
    font-size: 8px;
    font-weight: 700;
    letter-spacing: 0.04em;
    color: var(--text-ghost);
    margin-left: 4px;
  }
  .dragchip .dk:first-child {
    margin-left: 0;
  }
  .dragchip .dv {
    color: #fbbf24;
    font-weight: 700;
  }

  /* curvature comb + baked dots + off-field marks */
  .comb line {
    stroke: rgba(96, 165, 250, 0.5);
    stroke-width: 0.7;
    pointer-events: none;
  }
  .dots circle {
    pointer-events: none;
    opacity: 0.95;
  }
  .anchor .offring {
    fill: none;
    stroke: #f87171;
    stroke-width: 1.4;
    stroke-dasharray: 2 2;
  }
  .anchor .capring {
    fill: none;
    stroke: #38bdf8;
    stroke-width: 1.3;
  }
  .anchor .capring.stop {
    stroke: #f87171;
    stroke-width: 1.8;
  }

  /* 3-point arc tool */
  .arc-prev {
    fill: none;
    stroke: rgba(96, 165, 250, 0.9);
    stroke-width: 1.6;
    stroke-dasharray: 3 2;
  }
  .arc-pt {
    fill: #60a5fa;
  }
  .arc-lbl {
    fill: #bfdbfe;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-weight: 700;
    pointer-events: none;
  }
  .toolbanner {
    position: absolute;
    top: 6px;
    left: 50%;
    transform: translateX(-50%);
    font-size: 10px;
    font-weight: 600;
    color: #bfdbfe;
    background: rgba(30, 58, 138, 0.7);
    padding: 3px 10px;
    border-radius: var(--radius-small);
    pointer-events: none;
    box-shadow: 0 0 0 1px rgba(96, 165, 250, 0.4);
  }

  /* coordinate reference overlay */
  .coords .axis {
    stroke: rgba(96, 165, 250, 0.35);
    stroke-width: 1;
    stroke-dasharray: 3 3;
    pointer-events: none;
  }
  .coords .cdot {
    fill: rgba(147, 197, 253, 0.5);
    pointer-events: none;
  }
  .coords .clbl {
    fill: rgba(147, 197, 253, 0.7);
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-weight: 600;
    pointer-events: none;
  }

  /* top-left view-toggle chips */
  .vtoggles {
    position: absolute;
    top: 6px;
    left: 8px;
    display: flex;
    gap: 4px;
  }
  .vt {
    padding: 3px 8px;
    border: none;
    border-radius: var(--radius-small);
    background: rgba(0, 0, 0, 0.4);
    box-shadow: var(--ring);
    color: var(--text-muted);
    font-family: inherit;
    font-size: 9px;
    font-weight: 600;
    cursor: pointer;
    transition: color 0.12s ease, background 0.12s ease;
  }
  .vt:hover {
    color: var(--text-primary);
  }
  .vt.on {
    color: var(--bg-base);
    background: #fbbf24;
  }

  .dotchip {
    position: absolute;
    top: 26px;
    right: 8px;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 10px;
    color: #3ec97e;
    background: rgba(0, 0, 0, 0.5);
    padding: 2px 6px;
    border-radius: var(--radius-small);
    pointer-events: none;
    font-variant-numeric: tabular-nums;
  }

  .warn {
    position: absolute;
    top: 6px;
    left: 50%;
    transform: translateX(-50%);
    font-size: 10px;
    font-weight: 600;
    color: #fca5a5;
    background: rgba(69, 10, 10, 0.72);
    padding: 3px 9px;
    border-radius: var(--radius-small);
    pointer-events: none;
    box-shadow: 0 0 0 1px rgba(248, 113, 113, 0.35);
  }

  /* playback readout (t · s · v · κ) */
  .playread {
    position: absolute;
    bottom: 24px;
    left: 50%;
    transform: translateX(-50%);
    display: flex;
    align-items: baseline;
    gap: 5px;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 10px;
    color: var(--text-primary);
    background: rgba(0, 0, 0, 0.55);
    padding: 3px 10px;
    border-radius: var(--radius-small);
    pointer-events: none;
    font-variant-numeric: tabular-nums;
  }
  .playread .pk {
    font-size: 8px;
    font-weight: 700;
    color: var(--text-ghost);
    margin-left: 6px;
  }
  .playread .pk:first-child {
    margin-left: 0;
  }
  .playread .pv {
    color: #fbbf24;
    font-weight: 700;
  }
  .hint {
    position: absolute;
    bottom: 6px;
    left: 8px;
    font-size: 9px;
    color: var(--text-ghost);
    pointer-events: none;
  }

  .zoom {
    position: absolute;
    bottom: 6px;
    right: 8px;
    display: flex;
    gap: 3px;
    opacity: 0.55;
    transition: opacity 0.15s ease;
  }
  .zoom:hover,
  .zoom.on {
    opacity: 1;
  }
  .zoom button {
    width: 22px;
    height: 22px;
    border: none;
    border-radius: var(--radius-small);
    background: var(--bg-raised);
    box-shadow: var(--ring);
    color: var(--text-muted);
    font-size: 13px;
    line-height: 1;
    cursor: pointer;
    transition: color 0.15s ease, background 0.15s ease;
  }
  .zoom button:hover {
    color: var(--text-primary);
    background: var(--bg-surface);
  }

  /* playback bar: watch the robot run the path */
  /* timeline — same visual language as the Replay / Sandbox timeline */
  .timeline {
    flex-shrink: 0;
    display: flex;
    align-items: center;
    gap: 12px;
    margin-top: 8px;
    padding: 0 4px;
  }
  .play {
    flex-shrink: 0;
    width: 22px;
    height: 22px;
    border: none;
    border-radius: 50%;
    background: #1a1a1a;
    display: flex;
    align-items: center;
    justify-content: center;
    cursor: pointer;
    transition: background 0.15s ease;
  }
  .play:hover:not(:disabled) {
    background: #262626;
  }
  .play:disabled {
    opacity: 0.4;
    cursor: default;
  }
  .play svg {
    width: 11px;
    height: 11px;
    fill: #999;
  }
  .speed {
    flex-shrink: 0;
    min-width: 30px;
    border: none;
    border-radius: var(--radius-small);
    background: #1a1a1a;
    padding: 4px 6px;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9.5px;
    font-weight: 600;
    color: var(--text-dim);
    cursor: pointer;
    transition: background 0.15s ease, color 0.15s ease;
  }
  .speed:hover {
    background: #262626;
    color: var(--text-primary);
  }
  .speed.on {
    color: #fbbf24;
  }
  .body {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 3px;
    min-width: 0;
  }
  .track {
    position: relative;
    height: 20px;
    cursor: pointer;
    touch-action: none;
  }
  .rail {
    position: absolute;
    top: 50%;
    left: 0;
    right: 0;
    height: 5px;
    border-radius: 2.5px;
    background: var(--bg-base);
    box-shadow: var(--ring);
    transform: translateY(-50%);
  }
  .handle {
    position: absolute;
    top: 50%;
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: #fff;
    box-shadow: 0 1px 3px rgba(0, 0, 0, 0.7);
    transform: translate(-50%, -50%);
    z-index: 2;
  }
  .axis {
    position: relative;
    height: 12px;
  }
  .axis-tick {
    position: absolute;
    top: 0;
  }
  .tick-mark {
    position: absolute;
    top: 0;
    left: 0;
    width: 1px;
    height: 3px;
    background: rgba(255, 255, 255, 0.15);
  }
  .tick-label {
    position: absolute;
    top: 4px;
    left: 2px;
    font-size: 7px;
    color: var(--text-ghost);
    white-space: nowrap;
  }
  .clock {
    flex-shrink: 0;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9.5px;
    font-weight: 500;
    color: var(--text-dim);
    font-variant-numeric: tabular-nums;
    white-space: nowrap;
  }
  .clock-sep {
    opacity: 0.5;
    margin: 0 1px;
  }
  .pb-sep {
    opacity: 0.5;
    margin: 0 3px;
  }
</style>
