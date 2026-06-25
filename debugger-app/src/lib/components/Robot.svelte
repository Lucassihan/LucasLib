<script lang="ts">
  import { toScreen, defaultField, type FieldConfig } from '../field/coords';
  import { settings } from '$lib/stores/settings.svelte';
  import { chassisParts, trackingSnapYs, clampTrackingIndex } from '$lib/field/chassis';

  let {
    x,
    y,
    h,
    width = 15,
    length = 15,
    field = defaultField
  }: {
    x: number;
    y: number;
    h: number;
    width?: number;
    length?: number;
    field?: FieldConfig;
  } = $props();

  const p = $derived(toScreen(x, y, field));

  // Drive wheels + intake flexwheels in the robot's local frame (front = up).
  const parts = $derived(
    chassisParts(width, length, settings.driveWheelCount, settings.intakePosition, settings.sixWheelLayout)
  );

  // Tracking center (= center of rotation): its local-frame y offset from the
  // chassis center. The chassis is shifted by −tcY so this point sits at the
  // pose, and rotate(h) then spins the robot about it.
  const tcY = $derived.by(() => {
    const snaps = trackingSnapYs(length, settings.driveWheelCount, settings.sixWheelLayout);
    return snaps[clampTrackingIndex(settings.trackingCenterIndex, settings.driveWheelCount)] ?? 0;
  });
</script>

<!-- Robot frame. Local frame points "front = up"; rotate(h) turns it clockwise,
     matching heading 0 = north, clockwise positive. The inner translate puts the
     tracking center at the pose so rotation happens about it. -->
<g transform="translate({p.sx} {p.sy}) rotate({h})">
  <g transform="translate(0 {-tcY})">
    <rect
      x={-width / 2}
      y={-length / 2}
      width={width}
      height={length}
      rx="1.5"
      class="body"
      vector-effect="non-scaling-stroke"
    />

    <!-- drive wheels: hollow filleted rects inside the chassis -->
    {#each parts.wheels as w}
      <rect x={w.x} y={w.y} width={w.w} height={w.h} rx="0.5" class="wheel" vector-effect="non-scaling-stroke" />
    {/each}

    <!-- intake flexwheels: solid nubs straddling the intake edge -->
    {#each parts.flex as f}
      <rect x={f.x} y={f.y} width={f.w} height={f.h} rx="0.4" class="flex" />
    {/each}
  </g>

  <!-- tracking center / center of rotation marker at the pose -->
  <circle cx="0" cy="0" r="2.4" class="tc-ring" vector-effect="non-scaling-stroke" />
  <line x1="0" y1="-3.4" x2="0" y2="3.4" class="tc-cross" vector-effect="non-scaling-stroke" />
  <line x1="-3.4" y1="0" x2="3.4" y2="0" class="tc-cross" vector-effect="non-scaling-stroke" />
  <circle cx="0" cy="0" r="0.9" class="tc-dot" />
</g>

<style>
  .body {
    fill: rgba(204, 204, 204, 0.12);
    stroke: var(--path-actual);
    stroke-width: 1.5;
  }
  .wheel {
    fill: none;
    stroke: var(--path-actual);
    stroke-width: 1.2;
    opacity: 0.6;
  }
  .flex {
    fill: var(--path-actual);
    opacity: 0.85;
  }
  .tc-ring {
    fill: none;
    stroke: #fbbf24;
    stroke-width: 1.3;
  }
  .tc-cross {
    stroke: #fbbf24;
    stroke-width: 1;
  }
  .tc-dot {
    fill: #fbbf24;
  }
</style>
