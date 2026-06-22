<script lang="ts">
  // Compass-style heading dial: a graduated bezel with N·E·S·W labels and the
  // actual configured chassis rotated to the current heading. The intake side
  // shows facing (no separate heading line). Mirrors the field robot via the
  // shared chassisParts geometry.
  import { settings, holesToInches } from '$lib/stores/settings.svelte';
  import { chassisParts, CHASSIS } from '$lib/field/chassis';

  let { heading, size = 138 }: { heading: number; size?: number } = $props();

  // Dial geometry, in SVG units.
  const CX = 75;
  const CY = 54;
  const R = 42;

  const D2R = Math.PI / 180;
  function onRing(r: number, deg: number): [number, number] {
    const a = deg * D2R;
    return [CX + Math.sin(a) * r, CY - Math.cos(a) * r];
  }

  // Graduations every 10°, with longer/brighter marks at 30° and the cardinals.
  interface Tick {
    x1: number;
    y1: number;
    x2: number;
    y2: number;
    w: number;
    o: number;
  }
  const ticks: Tick[] = [];
  for (let d = 0; d < 360; d += 10) {
    let r1 = R - 3;
    let w = 0.9;
    let o = 0.35;
    if (d % 90 === 0) {
      r1 = R - 9;
      w = 1.3;
      o = 0.85;
    } else if (d % 30 === 0) {
      r1 = R - 5;
      w = 1.05;
      o = 0.55;
    }
    const a = onRing(r1, d);
    const b = onRing(R, d);
    ticks.push({ x1: a[0], y1: a[1], x2: b[0], y2: b[1], w, o });
  }

  const cardinals = [
    { d: 0, t: 'N' },
    { d: 90, t: 'E' },
    { d: 180, t: 'S' },
    { d: 270, t: 'W' }
  ].map((c) => {
    const p = onRing(R + 7, c.d);
    return { x: p[0], y: p[1], t: c.t };
  });

  // Robot hardware (inches, local frame) + a scale that fits it inside the ring.
  const width = $derived(holesToInches(settings.robotWidthHoles));
  const length = $derived(holesToInches(settings.robotLengthHoles));
  const parts = $derived(
    chassisParts(width, length, settings.driveWheelCount, settings.intakePosition)
  );
  const scale = $derived.by(() => {
    const bound = Math.hypot(width / 2, length / 2) + CHASSIS.FLEX_LEN / 2;
    return (R * 0.6) / bound;
  });
</script>

<svg
  viewBox="0 0 150 116"
  width={size}
  role="img"
  aria-label="Heading {heading.toFixed(1)} degrees"
>
  <circle cx={CX} cy={CY} r={R} class="ring" />

  {#each ticks as t}
    <line x1={t.x1} y1={t.y1} x2={t.x2} y2={t.y2} class="tick" stroke-width={t.w} opacity={t.o} />
  {/each}

  {#each cardinals as c}
    <text x={c.x} y={c.y} class="card-lbl">{c.t}</text>
  {/each}

  <g transform="translate({CX} {CY}) rotate({heading}) scale({scale})">
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
  </g>

  <circle cx={CX} cy={CY} r="1.4" class="center" />
</svg>

<style>
  svg {
    display: block;
    height: auto;
  }
  .ring {
    fill: none;
    stroke: var(--border-light);
    stroke-width: 1;
  }
  .tick {
    stroke: var(--text-dim);
    stroke-linecap: round;
  }
  .card-lbl {
    fill: var(--text-muted);
    font-size: 7.5px;
    font-weight: 600;
    text-anchor: middle;
    dominant-baseline: central;
    font-family: inherit;
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
  .center {
    fill: var(--path-actual);
  }
</style>
