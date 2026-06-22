<script lang="ts">
  import { toScreen, defaultField, type FieldConfig } from '../field/coords';
  import { settings } from '$lib/stores/settings.svelte';
  import { chassisParts } from '$lib/field/chassis';

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
    chassisParts(width, length, settings.driveWheelCount, settings.intakePosition)
  );
</script>

<!-- Robot frame. Local frame points "front = up"; rotate(h) turns it clockwise,
     matching heading 0 = north, clockwise positive. -->
<g transform="translate({p.sx} {p.sy}) rotate({h})">
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

  <!-- center dot; facing is read from the intake side, no heading line -->
  <circle cx="0" cy="0" r="1.3" class="center" />
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
  .center {
    fill: var(--path-actual);
  }
</style>
