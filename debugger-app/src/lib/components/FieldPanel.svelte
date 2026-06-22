<script lang="ts">
  import Robot from './Robot.svelte';
  import Path from './Path.svelte';
  import Waypoint from './Waypoint.svelte';
  import TurnOverlay from './TurnOverlay.svelte';
  import BoomerangOverlay from './BoomerangOverlay.svelte';
  import LyPoseOverlay from './LyPoseOverlay.svelte';
  import { settings, holesToInches } from '$lib/stores/settings.svelte';
  import { traceState, poseAt, movementEndHeading } from '$lib/stores/trace.svelte';
  import { endsAtTarget, isTurn, prettyMovement, movementColor } from '$lib/trace/labels';
  import { toScreen } from '$lib/field/coords';
  import type { Movement } from '$lib/trace/types';

  // VEX field: 144" × 144", 24" foam tiles (6 × 6 grid).
  const SIZE = 144;
  const lines = [24, 48, 72, 96, 120]; // interior grid lines

  // Distance-based "close" stage zone radius. Only moveTo / boomerang flip a
  // stage purely on distance to target (headingDisableDist); other movements
  // have no distance-based stage, so no circle.
  const hdd = $derived(traceState.trace.header.headingDisableDist || 6);
  function stageRadius(m: Movement): number {
    return m.type === 'moveTo' || m.type === 'boomerangTo' ? hdd : 0;
  }

  // Robot pose tracks the scrubber position over the loaded trace.
  const pose = $derived(poseAt(traceState.time));

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
    return `settle ${m.exitSmallError} in`;
  }
</script>

<section class="panel field">
  <div class="panel-label">Field</div>

  <div class="canvas">
    <svg viewBox="0 0 {SIZE} {SIZE}" preserveAspectRatio="xMidYMid meet">
      <!-- interior tile grid -->
      {#each lines as p}
        <line x1={p} y1="0" x2={p} y2={SIZE} class="grid" vector-effect="non-scaling-stroke" />
        <line x1="0" y1={p} x2={SIZE} y2={p} class="grid" vector-effect="non-scaling-stroke" />
      {/each}

      <!-- center cross, a touch brighter -->
      <line x1={SIZE / 2} y1="0" x2={SIZE / 2} y2={SIZE} class="center" vector-effect="non-scaling-stroke" />
      <line x1="0" y1={SIZE / 2} x2={SIZE} y2={SIZE / 2} class="center" vector-effect="non-scaling-stroke" />

      <!-- field perimeter -->
      <rect x="0" y="0" width={SIZE} height={SIZE} class="border" vector-effect="non-scaling-stroke" />

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
            smallError={m.exitSmallError}
            heading={approachHeading(m)}
            onenter={(e) => showTip(m, e)}
            onmove={moveTip}
            onleave={hideTip}
          />
        {/if}
      {/each}

      <!-- robot at the current scrub position; size driven by settings -->
      <Robot
        x={pose.x}
        y={pose.y}
        h={pose.h}
        width={holesToInches(settings.robotWidthHoles)}
        length={holesToInches(settings.robotLengthHoles)}
      />
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

  .grid {
    stroke: var(--border-subtle);
    stroke-width: 1;
  }

  .center {
    stroke: var(--border-light);
    stroke-width: 1;
  }

  .border {
    stroke: var(--text-ghost);
    stroke-width: 1;
    fill: none;
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
