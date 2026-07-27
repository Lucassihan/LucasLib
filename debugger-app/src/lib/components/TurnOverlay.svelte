<script lang="ts">
  import { toScreen, defaultField, type FieldConfig } from '../field/coords';
  import type { Movement } from '$lib/trace/types';
  import { movementBounds, movementTicks } from '$lib/stores/trace.svelte';
  import { movementColor } from '$lib/trace/labels';

  // ticks/bounds default to the replay trace store; the sandbox passes its own
  // simulated ticks so the overlay renders identically there.
  let {
    movement,
    trackwidth,
    field = defaultField,
    ticksOverride,
    boundsOverride
  }: {
    movement: Movement;
    trackwidth: number;
    field?: FieldConfig;
    ticksOverride?: { x: number; y: number; h: number }[];
    boundsOverride?: { start: { x: number; y: number; h: number }; end: { x: number; y: number; h: number } };
  } = $props();

  const srcTicks = () => ticksOverride ?? movementTicks(movement.index);
  const srcBounds = () => boundsOverride ?? movementBounds(movement.index);

  const D2R = Math.PI / 180;
  const wrap = (a: number) => {
    while (a > 180) a -= 360;
    while (a < -180) a += 360;
    return a;
  };
  const r2 = (n: number) => Math.round(n * 100) / 100;

  const color = $derived(movementColor(movement.type));
  const isSwing = $derived(movement.type.startsWith('swing'));

  // Heading h -> screen point at radius r from (cx, cy). Field +Y is up but SVG
  // y is flipped, so a heading points along (sin h, -cos h) on screen.
  function radPoint(cx: number, cy: number, r: number, h: number) {
    const a = h * D2R;
    return { x: cx + Math.sin(a) * r, y: cy - Math.cos(a) * r };
  }

  // SWING: the filled band is reconstructed from real odometry. For every
  // recorded tick we place the two drive-side wheels at center ± trackwidth/2
  // (perpendicular to that tick's heading) and fill the polygon between the
  // left-wheel and right-wheel polylines. This matches the actual displacement
  // exactly — partiality, drift and curvature all fall out for free.
  const band = $derived.by(() => {
    if (!isSwing) return null;
    const ticks = srcTicks();
    if (ticks.length < 2) return null;
    const tw2 = trackwidth / 2;

    const left: { sx: number; sy: number }[] = [];
    const right: { sx: number; sy: number }[] = [];
    for (const t of ticks) {
      const hr = t.h * D2R;
      const c = Math.cos(hr);
      const s = Math.sin(hr);
      // field right wheel = (x + cos h, y - sin h) * tw2 ; left = opposite
      left.push(toScreen(t.x - c * tw2, t.y + s * tw2, field));
      right.push(toScreen(t.x + c * tw2, t.y - s * tw2, field));
    }

    const toStr = (p: { sx: number; sy: number }) => `${r2(p.sx)},${r2(p.sy)}`;
    const poly = [...left, ...right.slice().reverse()].map(toStr).join(' ');
    const leftPath = left.map(toStr).join(' ');
    const rightPath = right.map(toStr).join(' ');
    return {
      poly,
      leftPath,
      rightPath,
      startA: left[0],
      startB: right[0],
      endA: left[left.length - 1],
      endB: right[right.length - 1]
    };
  });

  // SPOT TURN: an angular dial. Center is the (stationary) robot center; the
  // gauge band sweeps from the first tick's heading by the accumulated turn,
  // so it reads true rotation (incl. >180° and partial turns) from odometry.
  const dial = $derived.by(() => {
    if (isSwing) return null;
    const ticks = srcTicks();
    const b = srcBounds();
    if (!b || ticks.length < 2) return null;

    const c = toScreen(b.start.x, b.start.y, field);
    const startH = ticks[0].h;
    let sweep = 0;
    for (let i = 1; i < ticks.length; i++) sweep += wrap(ticks[i].h - ticks[i - 1].h);
    const drawn = Math.max(-350, Math.min(350, sweep));

    const R = 6; // inches — fixed legible gauge radius
    const p0 = radPoint(c.sx, c.sy, R, startH);
    const p1 = radPoint(c.sx, c.sy, R, startH + drawn);
    const large = Math.abs(drawn) > 180 ? 1 : 0;
    // field heading is clockwise-positive; on the flipped-Y screen that is also
    // a clockwise arc, i.e. SVG sweep-flag 1 for a positive (CW) turn.
    const flag = drawn >= 0 ? 1 : 0;

    return {
      cx: c.sx,
      cy: c.sy,
      R,
      bandPath: `M ${r2(p0.x)},${r2(p0.y)} A ${R},${R} 0 ${large} ${flag} ${r2(p1.x)},${r2(p1.y)}`,
      startTip: radPoint(c.sx, c.sy, R, startH),
      needle: radPoint(c.sx, c.sy, R, startH + drawn)
    };
  });
</script>

{#if band}
  <!-- swept chassis area between the two drive-side wheel paths -->
  <polygon points={band.poly} fill={color} fill-opacity="0.16" stroke="none" />
  <!-- the two wheel paths -->
  <polyline points={band.leftPath} fill="none" stroke={color} stroke-width="1.3" opacity="0.65" vector-effect="non-scaling-stroke" />
  <polyline points={band.rightPath} fill="none" stroke={color} stroke-width="1.3" opacity="0.65" vector-effect="non-scaling-stroke" />
  <!-- starting chassis line (dim) and final chassis line (bright leading edge) -->
  <line x1={band.startA.sx} y1={band.startA.sy} x2={band.startB.sx} y2={band.startB.sy} stroke={color} stroke-width="1" opacity="0.3" stroke-dasharray="2 2" vector-effect="non-scaling-stroke" />
  <line x1={band.endA.sx} y1={band.endA.sy} x2={band.endB.sx} y2={band.endB.sy} stroke={color} stroke-width="2" opacity="0.9" vector-effect="non-scaling-stroke" />
{/if}

{#if dial}
  <circle cx={dial.cx} cy={dial.cy} r={dial.R} fill="none" stroke={color} stroke-width="1" opacity="0.16" vector-effect="non-scaling-stroke" />
  <path d={dial.bandPath} fill="none" stroke={color} stroke-width="4" stroke-linecap="round" opacity="0.5" vector-effect="non-scaling-stroke" />
  <line x1={dial.cx} y1={dial.cy} x2={dial.startTip.x} y2={dial.startTip.y} stroke={color} stroke-width="1" opacity="0.3" stroke-dasharray="2 2" vector-effect="non-scaling-stroke" />
  <line x1={dial.cx} y1={dial.cy} x2={dial.needle.x} y2={dial.needle.y} stroke={color} stroke-width="2" opacity="0.9" vector-effect="non-scaling-stroke" />
  <circle cx={dial.cx} cy={dial.cy} r="1.8" fill={color} />
{/if}
