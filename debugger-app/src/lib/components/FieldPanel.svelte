<script lang="ts">
  import Robot from './Robot.svelte';
  import Path from './Path.svelte';
  import Waypoint from './Waypoint.svelte';
  import TurnOverlay from './TurnOverlay.svelte';
  import BoomerangOverlay from './BoomerangOverlay.svelte';
  import TrajectoryOverlay from './TrajectoryOverlay.svelte';
  import RamseteMoveOverlay from './RamseteMoveOverlay.svelte';
  import BatteryDots from './BatteryDots.svelte';
  import MotorTempBadge from './MotorTempBadge.svelte';
  import { untrack } from 'svelte';
  import { settings, holesToInches } from '$lib/stores/settings.svelte';
  import { traceState, poseAt, poseAtIn, tickAt, clearGhost, movementEndHeading } from '$lib/stores/trace.svelte';
  import { endsAtTarget, isTurn, isRamseteMove, isTrajectory, prettyMovement, movementColor } from '$lib/trace/labels';
  import { toScreen } from '$lib/field/coords';
  import { backdropSrc, fieldImageRect } from '$lib/field/backdrops';
  import { base } from '$app/paths';
  import type { Movement } from '$lib/trace/types';

  // VEX field: 144" × 144", 24" foam tiles (6 × 6 grid).
  const SIZE = 144;

  // Distance-based "close" stage zone radius. Only moveTo / ramsete.moveTo /
  // boomerang flip a stage purely on distance to target (headingDisableDist);
  // other movements have no distance-based stage, so no circle.
  const hdd = $derived(traceState.trace.header.headingDisableDist || 6);
  function stageRadius(m: Movement): number {
    return m.type === 'moveTo' || m.type === 'boomerangTo' || isRamseteMove(m.type) ? hdd : 0;
  }

  // Robot pose tracks the scrubber position over the loaded trace.
  const pose = $derived(poseAt(traceState.time));

  // Health at the scrub position (per-tick since trace v1.1); older traces
  // fall back to the battery-at-start header, temps just hide.
  const curTick = $derived(tickAt(traceState.time));
  const batPct = $derived(
    curTick && curTick.bat >= 0 ? curTick.bat : traceState.trace.header.batteryPct
  );
  const mtemp = $derived(curTick ? curTick.mtemp : -1);

  // Stage-flip pulse: when a staged movement latches its next stage under the
  // playhead, a ring pulses out of the robot at that spot. Guarded against
  // scrubbing jumps so dragging the timeline doesn't spam rings.
  let pulses = $state<{ id: number; x: number; y: number }[]>([]);
  let pulseId = 0;
  let prevStage = -1;
  let prevMi = -1;
  let prevTime = 0;
  $effect(() => {
    const tk = curTick;
    const now = traceState.time;
    if (!tk) return;
    const jumped = Math.abs(now - prevTime) > 200;
    prevTime = now;
    if (tk.mi === prevMi && tk.stage > prevStage && !jumped) {
      const p = { id: pulseId++, x: tk.x, y: tk.y };
      pulses = [...pulses, p];
      setTimeout(() => (pulses = pulses.filter((q) => q.id !== p.id)), 800);
    }
    prevMi = tk.mi;
    prevStage = tk.stage;
  });

  // Comparison ghost: full faint path + a translucent robot at the same
  // timestamp, so two runs of the same auton can be compared beat for beat.
  const ghost = $derived(traceState.ghost);
  const ghostPose = $derived(ghost ? poseAtIn(ghost.trace, traceState.time) : null);

  // Live divergence between the two runs at the playhead. "Hot" past 2 in —
  // that's the moment the runs stop being the same auton.
  const ghostDelta = $derived.by(() => {
    if (!ghostPose) return null;
    const din = Math.hypot(pose.x - ghostPose.x, pose.y - ghostPose.y);
    let dh = Math.abs(pose.h - ghostPose.h) % 360;
    if (dh > 180) dh = 360 - dh;
    return { din, dh, hot: din > 2 };
  });
  const ghostPoints = $derived(
    ghost
      ? ghost.trace.ticks
          .map((t) => {
            const p = toScreen(t.x, t.y);
            return `${p.sx},${p.sy}`;
          })
          .join(' ')
      : ''
  );

  // Top-down field render shown behind the path/robot. The PNG is scaled so its
  // playable TILE area (not the perimeter wall) maps onto the 144×144 field box,
  // keeping 1 tile = 24"; the wall falls just outside the viewBox and is clipped.
  const fieldImg = $derived(backdropSrc(settings.fieldBackdrop, base));
  const fieldRect = fieldImageRect(SIZE);

  // --- Follow cam: zoom into the robot and track it through the run. The viewBox
  // becomes a window (field size / zoom) centered on a SMOOTHED camera pose so
  // odometry jitter doesn't shake the view. POV lock additionally rotates the
  // world about the camera so the robot always points up (robot's-eye view).
  let follow = $state(false);
  let povLock = $state(false);
  let zoom = $state(2.5);
  const ZOOM_MIN = 1.5;
  const ZOOM_MAX = 4.5;
  const ZOOM_STEP = 0.5;

  // Smoothed camera pose (screen px + heading deg). A rAF loop eases it toward
  // the robot with a fixed time constant — frame-rate independent, and it works
  // during playback AND scrubbing. Snaps to the robot when tracking starts.
  let cam = $state({ x: 0, y: 0, h: 0 });
  const CAM_TAU = 0.14; // s — smaller = stiffer camera
  const wrapDeg = (a: number) => {
    while (a > 180) a -= 360;
    while (a < -180) a += 360;
    return a;
  };
  $effect(() => {
    if (!follow) return;
    // snap to the robot immediately (untracked — scrubbing must not re-run this)
    untrack(() => {
      const p = poseAt(traceState.time);
      const s = toScreen(p.x, p.y);
      cam = { x: s.sx, y: s.sy, h: p.h };
    });
    let raf = 0;
    let last = performance.now();
    const step = (now: number) => {
      const dt = Math.min((now - last) / 1000, 0.1);
      last = now;
      const p = poseAt(traceState.time);
      const s = toScreen(p.x, p.y);
      const a = 1 - Math.exp(-dt / CAM_TAU);
      const nx = cam.x + (s.sx - cam.x) * a;
      const ny = cam.y + (s.sy - cam.y) * a;
      const nh = cam.h + wrapDeg(p.h - cam.h) * a;
      if (Math.abs(nx - cam.x) + Math.abs(ny - cam.y) + Math.abs(nh - cam.h) > 1e-3) {
        cam = { x: nx, y: ny, h: nh };
      }
      raf = requestAnimationFrame(step);
    };
    raf = requestAnimationFrame(step);
    return () => cancelAnimationFrame(raf);
  });

  const camViewBox = $derived.by(() => {
    const f = fieldRect;
    if (!follow) return `${f.x} ${f.y} ${f.w} ${f.h}`;
    const w = f.w / zoom;
    const h = f.h / zoom;
    let x = cam.x - w / 2;
    let y = cam.y - h / 2;
    // keep the window inside the rendered field (+ border) rather than centering
    // the robot when it's near an edge — same as a game camera's edge clamp.
    // POV mode skips the clamp: the robot stays centered while the world spins.
    if (!povLock) {
      x = Math.max(f.x, Math.min(x, f.x + f.w - w));
      y = Math.max(f.y, Math.min(y, f.y + f.h - h));
    }
    return `${x} ${y} ${w} ${h}`;
  });

  // POV lock: counter-rotate the whole scene about the camera by the smoothed
  // heading, so the robot reads as fixed and the field moves around it.
  const worldTransform = $derived(
    follow && povLock ? `rotate(${(-cam.h).toFixed(3)} ${cam.x.toFixed(2)} ${cam.y.toFixed(2)})` : undefined
  );

  function toggleFollow() {
    follow = !follow;
    if (!follow) povLock = false;
  }
  function togglePov() {
    povLock = !povLock;
    if (povLock) follow = true;
  }
  function zoomBy(d: number) {
    zoom = Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, Math.round((zoom + d) * 10) / 10));
    follow = true; // zooming implies you want to be on the robot
  }

  // f = toggle follow, p = POV lock, +/- = zoom (ignored while typing in a field)
  function onCamKey(e: KeyboardEvent) {
    const t = e.target instanceof HTMLElement ? e.target : null;
    if (t && t.closest('input, textarea, select, [contenteditable="true"]')) return;
    if (e.key === 'f' || e.key === 'F') toggleFollow();
    else if (e.key === 'p' || e.key === 'P') togglePov();
    else if (e.key === '+' || e.key === '=') zoomBy(ZOOM_STEP);
    else if (e.key === '-' || e.key === '_') zoomBy(-ZOOM_STEP);
  }

  // perp-line orientation: boomerang faces its targetH; others approach along
  // their final heading.
  function approachHeading(m: Movement): number {
    return m.type === 'boomerangTo' ? m.targetH : movementEndHeading(m.index);
  }

  // waypoint hover tooltip
  let tip = $state<{ m: Movement; x: number; y: number } | null>(null);
  function showTip(m: Movement, e: PointerEvent) {
    tip = { m, x: e.clientX, y: e.clientY };
  }
  function moveTip(e: PointerEvent) {
    if (tip) tip = { ...tip, x: e.clientX, y: e.clientY };
  }
  function hideTip() {
    tip = null;
  }

  function exitDetail(m: Movement): string {
    if (m.exitPerpLine > 0) return `perp line ${m.exitPerpLine} in`;
    if (m.exitDist > 0) return `exit dist ${m.exitDist} in`;
    return `settle |D| < ${m.settleD}`;
  }
</script>

<svelte:window onkeydown={onCamKey} />

<section class="panel field">
  <div class="panel-label">Field</div>

  <!-- top-right HUD: comparison run + divergence + battery/temps at the playhead -->
  <div class="hud">
    {#if ghost}
      <button class="ghost-chip" onclick={clearGhost} title="Comparison run — click to unpin">
        <svg viewBox="0 0 12 12" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linejoin="round">
          <path d="M6 1.2 C3.5 1.2 2 3.1 2 5.4 V10.6 L3.35 9.4 L4.7 10.6 L6 9.4 L7.3 10.6 L8.65 9.4 L10 10.6 V5.4 C10 3.1 8.5 1.2 6 1.2 Z" />
        </svg>
        <span class="ghost-name">{ghost.name}</span>
        <span class="ghost-x">×</span>
      </button>
      {#if ghostDelta}
        <span class="gd" class:hot={ghostDelta.hot} title="Gap between the two runs at the playhead">
          Δ {ghostDelta.din.toFixed(1)} in · {ghostDelta.dh.toFixed(1)}°
        </span>
      {/if}
    {/if}
    {#if mtemp >= 0}
      <MotorTempBadge temp={mtemp} />
    {/if}
    {#if batPct >= 0}
      <BatteryDots pct={batPct} volts={traceState.trace.header.batteryVolts} />
    {/if}
  </div>

  <div class="canvas">
    <svg viewBox={camViewBox} preserveAspectRatio="xMidYMid meet">
      <!-- POV lock spins the whole scene about the camera; robot stays upright -->
      <g transform={worldTransform}>
      <image href={fieldImg} x={fieldRect.x} y={fieldRect.y} width={fieldRect.w} height={fieldRect.h} preserveAspectRatio="none" />

      <!-- comparison ghost run: faint dashed path under everything, plus a
           translucent robot tracking the same timestamp -->
      {#if ghost}
        <polyline
          points={ghostPoints}
          class="ghost-path"
          fill="none"
          vector-effect="non-scaling-stroke"
        />
      {/if}

      <!-- the schedule's planned + served reference curves, UNDER the driven
           path: the plan is the backdrop, the run is the subject -->
      {#each traceState.trace.movements as m (m.index)}
        {#if isTrajectory(m.type)}
          <TrajectoryOverlay movement={m} layer="paths" />
        {/if}
      {/each}

      <!-- actual (odometry) path, colored by movement -->
      <Path ticks={traceState.trace.ticks} movements={traceState.trace.movements} />

      <!-- turn overlays (spot turns + swing turns) -->
      {#each traceState.trace.movements as m (m.index)}
        {#if isTurn(m.type)}
          <TurnOverlay movement={m} trackwidth={traceState.trace.header.trackwidth || 11.5} />
        {/if}
      {/each}

      <!-- boomerang lead-point geometry for the boomerang under the scrubber -->
      {#each traceState.trace.movements as m (m.index)}
        {#if m.type === 'boomerangTo'}
          <BoomerangOverlay movement={m} />
        {/if}
      {/each}

      <!-- the reference pose itself stays on top, where it can be read -->
      {#each traceState.trace.movements as m (m.index)}
        {#if isTrajectory(m.type)}
          <TrajectoryOverlay movement={m} layer="ghost" />
        {/if}
      {/each}

      <!-- point-chase geometry for the ramsete.moveTo under the scrubber -->
      {#each traceState.trace.movements as m (m.index)}
        {#if isRamseteMove(m.type)}
          <RamseteMoveOverlay movement={m} />
        {/if}
      {/each}

      <!-- commanded waypoints for movements that end at an (x, y) target -->
      {#each traceState.trace.movements as m (m.index)}
        {#if endsAtTarget(m.type)}
          {#if stageRadius(m) > 0}
            {@const sp = toScreen(m.targetX, m.targetY)}
            <circle
              cx={sp.sx}
              cy={sp.sy}
              r={stageRadius(m)}
              fill="none"
              stroke={movementColor(m.type)}
              stroke-width="1"
              stroke-dasharray="2 2"
              opacity="0.55"
              vector-effect="non-scaling-stroke"
            />
          {/if}
          <Waypoint
            x={m.targetX}
            y={m.targetY}
            settleR={m.exitDist}
            crossD={m.exitPerpLine}
            backward={m.backwards}
            heading={approachHeading(m)}
            onenter={(e) => showTip(m, e)}
            onmove={moveTip}
            onleave={hideTip}
          />
        {/if}
      {/each}

      {#if ghost && ghostPose}
        <g class="ghost-robot">
          <Robot
            x={ghostPose.x}
            y={ghostPose.y}
            h={ghostPose.h}
            width={holesToInches(settings.robotWidthHoles)}
            length={holesToInches(settings.robotLengthHoles)}
          />
        </g>
        <!-- tether: main robot ↔ ghost, heats up as the runs split -->
        {#if ghostDelta}
          {@const a = toScreen(pose.x, pose.y)}
          {@const b = toScreen(ghostPose.x, ghostPose.y)}
          <line
            x1={a.sx} y1={a.sy} x2={b.sx} y2={b.sy}
            class="tether"
            class:hot={ghostDelta.hot}
            vector-effect="non-scaling-stroke"
          />
        {/if}
      {/if}

      <!-- robot at the current scrub position; size driven by settings -->
      <Robot
        x={pose.x}
        y={pose.y}
        h={pose.h}
        width={holesToInches(settings.robotWidthHoles)}
        length={holesToInches(settings.robotLengthHoles)}
      />

      <!-- stage-flip pulse rings -->
      {#each pulses as p (p.id)}
        {@const sp = toScreen(p.x, p.y)}
        <circle class="stage-pulse" cx={sp.sx} cy={sp.sy} r="5" vector-effect="non-scaling-stroke" />
      {/each}
      </g>
    </svg>
  </div>

  <!-- follow cam: toggle tracking + POV heading lock + zoom in/out on the robot -->
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
      <button class="z" onclick={() => zoomBy(-ZOOM_STEP)} disabled={follow && zoom <= ZOOM_MIN} aria-label="Zoom out">−</button>
      <span class="zlvl">{zoom.toFixed(1)}×</span>
      <button class="z" onclick={() => zoomBy(ZOOM_STEP)} disabled={follow && zoom >= ZOOM_MAX} aria-label="Zoom in">+</button>
    </div>
  </div>

  {#if tip}
    <div class="wp-tip" style="left: {tip.x}px; top: {tip.y}px;">
      <span class="wp-name">{prettyMovement(tip.m.type)}</span>
      <span class="wp-target">({tip.m.targetX}, {tip.m.targetY})</span>
      <span class="wp-exit">{exitDetail(tip.m)}</span>
    </div>
  {/if}
</section>

<style>
  .field {
    flex: 1;
    min-height: 0;
  }

  .hud {
    position: absolute;
    top: 8px;
    right: 12px;
    z-index: 2;
    display: flex;
    align-items: center;
    gap: 14px;
  }

  .ghost-chip {
    display: flex;
    align-items: center;
    gap: 5px;
    background: var(--bg-raised);
    border: none;
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 3px 8px;
    color: var(--text-dim);
    font-family: inherit;
    cursor: pointer;
    transition: color 0.15s ease;
  }
  .ghost-chip:hover {
    color: var(--text-primary);
  }
  .ghost-chip svg {
    width: 10px;
    height: 10px;
    opacity: 0.8;
  }
  .ghost-name {
    font-size: 9px;
    font-weight: 600;
    max-width: 130px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .ghost-x {
    font-size: 11px;
    line-height: 1;
    color: var(--text-ghost);
  }
  .ghost-chip:hover .ghost-x {
    color: var(--text-primary);
  }

  .ghost-path {
    stroke: #b0b0c8;
    stroke-width: 1.2;
    stroke-dasharray: 5 4;
    stroke-linejoin: round;
    stroke-linecap: round;
    opacity: 0.5;
  }
  .ghost-robot {
    opacity: 0.3;
  }

  .gd {
    font-size: 9px;
    font-weight: 600;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-variant-numeric: tabular-nums;
    color: var(--text-dim);
    white-space: nowrap;
  }
  .gd.hot {
    color: #f87171;
  }

  .tether {
    stroke: #909090;
    stroke-width: 1;
    stroke-dasharray: 2 2;
    opacity: 0.7;
  }
  .tether.hot {
    stroke: #f87171;
    stroke-width: 1.4;
  }

  .stage-pulse {
    fill: none;
    stroke: var(--accent);
    stroke-width: 1.6;
    pointer-events: none;
    transform-box: fill-box;
    transform-origin: center;
    animation: stage-pulse 0.75s cubic-bezier(0.2, 0.7, 0.4, 1) forwards;
  }
  @keyframes stage-pulse {
    from {
      transform: scale(1);
      opacity: 0.9;
    }
    to {
      transform: scale(8);
      opacity: 0;
    }
  }

  /* follow-cam control, bottom-right of the field */
  .cam {
    position: absolute;
    bottom: 10px;
    right: 12px;
    z-index: 3;
    display: flex;
    align-items: center;
    gap: 6px;
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
    opacity: 0.4;
    transition: opacity 0.15s ease;
  }
  .cam.on .cam-zoom {
    opacity: 1;
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

  .canvas {
    position: absolute;
    inset: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    /* extra top room so the field clears the "Field" label (matches the
       graph panels' title clearance) */
    padding: 32px 16px 16px;
  }

  svg {
    width: 100%;
    height: 100%;
    max-width: 100%;
    max-height: 100%;
  }

  /* field render fills the square; round its corners to match the panel */
  image {
    border-radius: 6px;
  }

  /* waypoint hover tooltip — fixed so it floats free of the panel's clipping */
  .wp-tip {
    position: fixed;
    z-index: 50;
    transform: translate(-50%, calc(-100% - 12px));
    display: flex;
    flex-direction: column;
    gap: 1px;
    padding: 6px 10px;
    background: var(--bg-raised);
    border-radius: var(--radius-small);
    box-shadow:
      var(--ring),
      0 6px 20px rgba(0, 0, 0, 0.45);
    pointer-events: none;
    white-space: nowrap;
  }
  .wp-name {
    font-size: 11px;
    font-weight: 600;
    color: var(--text-primary);
  }
  .wp-target {
    font-size: 9px;
    font-weight: 400;
    color: var(--text-dim);
  }
  .wp-exit {
    font-size: 9px;
    font-weight: 400;
    color: var(--text-secondary);
  }
</style>
