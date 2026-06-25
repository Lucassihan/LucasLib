<script lang="ts">
  // Static top-down preview of the configured chassis (front = up). Mirrors the
  // field Robot exactly via the shared chassisParts geometry, so changing wheel
  // count / intake side / robot size updates this live.
  import { settings, holesToInches } from '$lib/stores/settings.svelte';
  import { chassisParts, CHASSIS } from '$lib/field/chassis';

  const width = $derived(holesToInches(settings.robotWidthHoles));
  const length = $derived(holesToInches(settings.robotLengthHoles));
  const parts = $derived(
    chassisParts(width, length, settings.driveWheelCount, settings.intakePosition, settings.sixWheelLayout)
  );

  // Symmetric padding (flex overhang counted on both sides) keeps the robot
  // centered in the box regardless of intake side.
  const PAD = 1.5;
  const halfW = $derived(width / 2 + PAD);
  const halfL = $derived(length / 2 + CHASSIS.FLEX_LEN / 2 + PAD);
  const vb = $derived(`${-halfW} ${-halfL} ${halfW * 2} ${halfL * 2}`);
</script>

<svg viewBox={vb} preserveAspectRatio="xMidYMid meet" aria-label="Robot preview">
  <rect
    x={-width / 2}
    y={-length / 2}
    width={width}
    height={length}
    rx="1.5"
    class="body"
    vector-effect="non-scaling-stroke"
  />

  {#each parts.wheels as w}
    <rect x={w.x} y={w.y} width={w.w} height={w.h} rx="0.5" class="wheel" vector-effect="non-scaling-stroke" />
  {/each}

  {#each parts.flex as f}
    <rect x={f.x} y={f.y} width={f.w} height={f.h} rx="0.4" class="flex" />
  {/each}

  <circle cx="0" cy="0" r="1.3" class="center" />
</svg>

<style>
  svg {
    width: 100%;
    height: 100%;
  }
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
