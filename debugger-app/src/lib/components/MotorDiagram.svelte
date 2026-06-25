<script lang="ts">
  // Governed motor output as a mini tank-drive schematic: the user-configured
  // chassis (front up) flanked by a vertical bipolar bar per side. Each bar is
  // exactly as tall as the chassis; forward fills up from centre, reverse down.
  import { settings, holesToInches } from '$lib/stores/settings.svelte';
  import { chassisParts, CHASSIS } from '$lib/field/chassis';

  let { left, right }: { left: number; right: number } = $props();

  const width = $derived(holesToInches(settings.robotWidthHoles));
  const length = $derived(holesToInches(settings.robotLengthHoles));
  const parts = $derived(
    chassisParts(width, length, settings.driveWheelCount, settings.intakePosition)
  );

  // Layout (inches). Bars sit just outside the chassis, full body height.
  const GAP = 1.4;
  const BARW = 1.3;
  const PAD_X = 5; // room so the value labels don't clip past the (skinny) bars
  const PAD_TOP = 1.2;
  const PAD_BOT = 4.5; // room for the value labels under the bars

  const halfX = $derived(width / 2 + GAP + BARW);
  const topY = $derived(-(length / 2 + CHASSIS.FLEX_LEN / 2));
  const vb = $derived(
    `${-halfX - PAD_X} ${topY - PAD_TOP} ${(halfX + PAD_X) * 2} ${length / 2 - (topY - PAD_TOP) + PAD_BOT}`
  );

  const leftBarX = $derived(-(width / 2 + GAP + BARW));
  const rightBarX = $derived(width / 2 + GAP);

  // Fill rect on a track running −length/2…+length/2 (centre = 0 = stopped).
  function fillRect(v: number): { y: number; h: number } {
    const h = Math.min(Math.abs(v) / 12, 1) * (length / 2);
    if (v >= 0) return { y: -h, h }; // forward — up from centre
    return { y: 0, h }; // reverse — down from centre
  }
  const lf = $derived(fillRect(left));
  const rf = $derived(fillRect(right));
  const sgn = (n: number) => `${n >= 0 ? '+' : '−'}${Math.abs(n).toFixed(1)}`;
  // Label font sized relative to the chassis so it stays legible when the whole
  // diagram is scaled down to the left column's height.
  const fontSize = $derived(Math.max(2.8, length * 0.2));
</script>

<svg viewBox={vb} class="md" xmlns="http://www.w3.org/2000/svg">
  <!-- left motor bar -->
  <rect x={leftBarX} y={-length / 2} width={BARW} height={length} rx="0.7" class="track" />
  <rect x={leftBarX} y={lf.y} width={BARW} height={lf.h} rx="0.5" class="vfill" />
  <line x1={leftBarX} y1="0" x2={leftBarX + BARW} y2="0" class="vtick" vector-effect="non-scaling-stroke" />
  <text x={leftBarX + BARW / 2} y={length / 2 + PAD_BOT * 0.7} class="mval" font-size={fontSize}>{sgn(left)}</text>

  <!-- right motor bar -->
  <rect x={rightBarX} y={-length / 2} width={BARW} height={length} rx="0.7" class="track" />
  <rect x={rightBarX} y={rf.y} width={BARW} height={rf.h} rx="0.5" class="vfill" />
  <line x1={rightBarX} y1="0" x2={rightBarX + BARW} y2="0" class="vtick" vector-effect="non-scaling-stroke" />
  <text x={rightBarX + BARW / 2} y={length / 2 + PAD_BOT * 0.7} class="mval" font-size={fontSize}>{sgn(right)}</text>

  <!-- configured chassis, front up -->
  <rect x={-width / 2} y={-length / 2} width={width} height={length} rx="1.5" class="body" vector-effect="non-scaling-stroke" />
  {#each parts.wheels as w}
    <rect x={w.x} y={w.y} width={w.w} height={w.h} rx="0.5" class="wheel" vector-effect="non-scaling-stroke" />
  {/each}
  {#each parts.flex as f}
    <rect x={f.x} y={f.y} width={f.w} height={f.h} rx="0.4" class="flex" />
  {/each}
</svg>

<style>
  .md {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
  }
  .track {
    fill: var(--bg-base);
  }
  .vfill {
    fill: #e8e8e8;
  }
  .vtick {
    stroke: rgba(255, 255, 255, 0.4);
    stroke-width: 1;
  }
  .mval {
    fill: var(--text-primary);
    font-weight: 600;
    text-anchor: middle;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
  }
  .body {
    fill: rgba(204, 204, 204, 0.13);
    stroke: var(--path-actual);
    stroke-width: 1.3;
  }
  .wheel {
    fill: none;
    stroke: var(--path-actual);
    stroke-width: 1.1;
    opacity: 0.6;
  }
  .flex {
    fill: var(--path-actual);
    opacity: 0.85;
  }
</style>
