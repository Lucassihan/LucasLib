<script lang="ts">
  // Tracking-center (center-of-rotation) picker: a chassis preview with a notched
  // slider beside it. Notches sit at every wheel row AND the midpoint between
  // adjacent rows, vertically aligned with the wheels. Click a notch to select;
  // the amber ⊕ on the chassis shows where the center of rotation lands.
  import { settings, holesToInches } from '$lib/stores/settings.svelte';
  import { chassisParts, CHASSIS, trackingSnapYs, clampTrackingIndex } from '$lib/field/chassis';

  const width = $derived(holesToInches(settings.robotWidthHoles));
  const length = $derived(holesToInches(settings.robotLengthHoles));
  const parts = $derived(
    chassisParts(width, length, settings.driveWheelCount, settings.intakePosition, settings.sixWheelLayout)
  );
  const snaps = $derived(trackingSnapYs(length, settings.driveWheelCount, settings.sixWheelLayout));
  const sel = $derived(clampTrackingIndex(settings.trackingCenterIndex, settings.driveWheelCount));
  const tcY = $derived(snaps[sel] ?? 0);

  // SVG layout: chassis on the left, slider track on the right.
  const CX = 54;
  const CY = 70;
  const TRACK_X = 120;
  const scale = $derived(
    Math.min(108 / (length + CHASSIS.FLEX_LEN), 78 / width)
  );
  const sy = (localY: number) => CY + localY * scale;

  function pick(i: number) {
    settings.trackingCenterIndex = i;
  }
</script>

<svg viewBox="0 0 150 140" width="100%" xmlns="http://www.w3.org/2000/svg">
  <!-- chassis -->
  <g transform="translate({CX} {CY}) scale({scale})">
    <rect x={-width / 2} y={-length / 2} width={width} height={length} rx="1.5" class="body" vector-effect="non-scaling-stroke" />
    {#each parts.wheels as w}
      <rect x={w.x} y={w.y} width={w.w} height={w.h} rx="0.5" class="wheel" vector-effect="non-scaling-stroke" />
    {/each}
    {#each parts.flex as f}
      <rect x={f.x} y={f.y} width={f.w} height={f.h} rx="0.4" class="flex" />
    {/each}
  </g>

  <!-- centerline (the tracking center always rides this) -->
  <line x1={CX} y1={sy(snaps[0])} x2={CX} y2={sy(snaps[snaps.length - 1])} class="centerline" />

  <!-- slider track -->
  <line x1={TRACK_X} y1={sy(snaps[0])} x2={TRACK_X} y2={sy(snaps[snaps.length - 1])} class="track" />

  <!-- guides + clickable notches at each snap (even index = wheel row) -->
  {#each snaps as snapY, i}
    <line x1={CX} y1={sy(snapY)} x2={TRACK_X} y2={sy(snapY)} class="guide" class:row={i % 2 === 0} />
    <circle cx={TRACK_X} cy={sy(snapY)} r={i % 2 === 0 ? 2.4 : 1.8} class="notch" class:row={i % 2 === 0} />
    {#if i === sel}<circle cx={TRACK_X} cy={sy(snapY)} r="4.2" class="thumb" />{/if}
    <circle cx={TRACK_X} cy={sy(snapY)} r="8" class="hit" role="button" tabindex="0" aria-label={`Tracking center position ${i + 1}`} onclick={() => pick(i)} />
  {/each}

  <!-- tracking center marker on the chassis centerline -->
  <circle cx={CX} cy={sy(tcY)} r="3" class="tc-ring" vector-effect="non-scaling-stroke" />
  <line x1={CX} y1={sy(tcY) - 4.2} x2={CX} y2={sy(tcY) + 4.2} class="tc-cross" />
  <line x1={CX - 4.2} y1={sy(tcY)} x2={CX + 4.2} y2={sy(tcY)} class="tc-cross" />
  <circle cx={CX} cy={sy(tcY)} r="1" class="tc-dot" />
</svg>

<style>
  svg {
    display: block;
  }
  .body {
    fill: rgba(204, 204, 204, 0.1);
    stroke: var(--path-actual);
    stroke-width: 1.3;
  }
  .wheel {
    fill: none;
    stroke: var(--path-actual);
    stroke-width: 1.1;
    opacity: 0.55;
  }
  .flex {
    fill: var(--path-actual);
    opacity: 0.7;
  }
  .centerline {
    stroke: var(--text-dim);
    stroke-width: 0.8;
    stroke-dasharray: 3 3;
    opacity: 0.4;
  }
  .track {
    stroke: #3a3a3a;
    stroke-width: 2;
  }
  .guide {
    stroke: var(--text-ghost);
    stroke-width: 0.8;
    opacity: 0.18;
    stroke-dasharray: 2 2;
  }
  .guide.row {
    opacity: 0.32;
    stroke-dasharray: none;
  }
  .notch {
    fill: #6a6a6a;
  }
  .notch.row {
    fill: #9a9a9a;
  }
  .thumb {
    fill: #fbbf24;
  }
  .hit {
    fill: transparent;
    cursor: pointer;
  }
  .tc-ring {
    fill: none;
    stroke: #fbbf24;
    stroke-width: 1.3;
  }
  .tc-cross {
    stroke: #fbbf24;
    stroke-width: 0.9;
  }
  .tc-dot {
    fill: #fbbf24;
  }
</style>
