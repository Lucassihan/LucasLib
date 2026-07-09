<script lang="ts">
  import Robot from './Robot.svelte';
  import Path from './Path.svelte';
  import Waypoint from './Waypoint.svelte';
  import TurnOverlay from './TurnOverlay.svelte';
  import BoomerangOverlay from './BoomerangOverlay.svelte';
  import LyPoseOverlay from './LyPoseOverlay.svelte';
  import BatteryDots from './BatteryDots.svelte';
  import MotorTempBadge from './MotorTempBadge.svelte';
  import { settings, holesToInches } from '$lib/stores/settings.svelte';
  import { traceState, poseAt, poseAtIn, tickAt, clearGhost, movementEndHeading } from '$lib/stores/trace.svelte';
  import { endsAtTarget, isTurn, prettyMovement, movementColor } from '$lib/trace/labels';
  import { toScreen } from '$lib/field/coords';
  import { backdropSrc, fieldImageRect } from '$lib/field/backdrops';
  import { base } from '$app/paths';
  import type { Movement } from '$lib/trace/types';

  // VEX field: 144" × 144", 24" foam tiles (6 × 6 grid).
  const SIZE = 144;

  // Distance-based "close" stage zone radius. Only moveTo / boomerang flip a
  // stage purely on distance to target (headingDisableDist); other movements
  // have no distance-based stage, so no circle.
  const hdd = $derived(traceState.trace.header.headingDisableDist || 6);
  function stageRadius(m: Movement): number {
    return m.type === 'moveTo' || m.type === 'boomerangTo' ? hdd : 0;
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
    <svg viewBox="{fieldRect.x} {fieldRect.y} {fieldRect.w} {fieldRect.h}" preserveAspectRatio="xMidYMid meet">
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

      <!-- lyPose path-follow geometry for the lyPose under the scrubber -->
      {#each traceState.trace.movements as m (m.index)}
        {#if m.type === 'lyPose'}
          <LyPoseOverlay movement={m} />
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
            exitDist={m.exitDist}
            perpLine={m.exitPerpLine}
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
    </svg>
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
