<script lang="ts">
  import { toScreen, defaultField, type FieldConfig } from '../field/coords';

  // A commanded target marker (dot) whose "halo" encodes the movement's spatial
  // exit condition:
  //   - exitDist  -> a circle of that radius (settle when within it)
  //   - perpLine  -> the perpendicular exit line (⊥ heading) + a fainter
  //                  proximity circle of radius perpLine (exits when the robot's
  //                  distance to the line drops to perpLine)
  //   - otherwise -> a faint settle-tolerance circle (smallError)
  let {
    x,
    y,
    exitDist = 0,
    perpLine = 0,
    smallError = 0,
    heading = 0,
    field = defaultField,
    onenter,
    onmove,
    onleave
  }: {
    x: number;
    y: number;
    exitDist?: number;
    perpLine?: number;
    smallError?: number;
    heading?: number;
    field?: FieldConfig;
    onenter?: (e: PointerEvent) => void;
    onmove?: (e: PointerEvent) => void;
    onleave?: (e: PointerEvent) => void;
  } = $props();

  const p = $derived(toScreen(x, y, field));

  // Perpendicular line endpoints: ⊥ to forward (sin h, cos h) is (cos h, −sin h),
  // spanning ±perpLine through the target.
  const perp = $derived.by(() => {
    const hr = (heading * Math.PI) / 180;
    const ex = Math.cos(hr);
    const ey = -Math.sin(hr);
    const a = toScreen(x + ex * perpLine, y + ey * perpLine, field);
    const b = toScreen(x - ex * perpLine, y - ey * perpLine, field);
    return { ax: a.sx, ay: a.sy, bx: b.sx, by: b.sy };
  });

  const fallbackR = $derived(exitDist <= 0 && perpLine <= 0 ? smallError : 0);
  const hitR = $derived(Math.max(perpLine, exitDist, smallError, 4) + 2);
</script>

<g class="wp" onpointerenter={onenter} onpointermove={onmove} onpointerleave={onleave}>
  <!-- generous transparent hit area for hovering -->
  <circle cx={p.sx} cy={p.sy} r={hitR} fill="transparent" />

  {#if exitDist > 0}
    <circle cx={p.sx} cy={p.sy} r={exitDist} class="exit-dist" vector-effect="non-scaling-stroke" />
  {/if}

  {#if perpLine > 0}
    <circle cx={p.sx} cy={p.sy} r={perpLine} class="activation" vector-effect="non-scaling-stroke" />
    <line x1={perp.ax} y1={perp.ay} x2={perp.bx} y2={perp.by} class="perp" vector-effect="non-scaling-stroke" />
  {/if}

  {#if fallbackR > 0}
    <circle cx={p.sx} cy={p.sy} r={fallbackR} class="settle" vector-effect="non-scaling-stroke" />
  {/if}

  <circle cx={p.sx} cy={p.sy} r="1.6" class="dot" />
</g>

<style>
  .wp {
    cursor: pointer;
  }
  .dot {
    fill: var(--text-secondary);
  }
  .exit-dist {
    fill: none;
    stroke: var(--text-dim);
    stroke-width: 1.1;
  }
  /* activation radius — the "weaker" line: faint + dashed */
  .activation {
    fill: none;
    stroke: var(--text-ghost);
    stroke-width: 1;
    stroke-dasharray: 3 3;
  }
  .perp {
    stroke: var(--text-secondary);
    stroke-width: 1.3;
  }
  .settle {
    fill: none;
    stroke: var(--text-ghost);
    stroke-width: 1;
  }
</style>
