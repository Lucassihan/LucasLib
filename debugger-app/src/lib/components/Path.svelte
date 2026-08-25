<script lang="ts">
  import { toScreen, defaultField, type FieldConfig } from '../field/coords';
  import { movementColor, isTrajectory, isRamseteMove } from '$lib/trace/labels';
  import { speedColor, speedRange } from '$lib/trace/speedRamp';
  import type { Tick, Movement } from '$lib/trace/types';

  let {
    ticks,
    movements,
    field = defaultField
  }: {
    ticks: Tick[];
    movements: Movement[];
    field?: FieldConfig;
  } = $props();

  // Only the RAMSETE movements command a speed, so only they have a measured
  // speed worth reading off the path. Everything else drives voltage straight
  // and a speed ramp there would be coloring noise.
  const isRamsete = (type: string) => isTrajectory(type) || isRamseteMove(type);

  // The actual path is the recorded odometry points connected — one polyline
  // per movement, colored by movement type. Each segment borrows the next
  // movement's first point so the colored runs join without a gap.
  const grouped = $derived.by(() => {
    const byMi = new Map<number, Tick[]>();
    for (const t of ticks) {
      const arr = byMi.get(t.mi);
      if (arr) arr.push(t);
      else byMi.set(t.mi, [t]);
    }

    const mis = [...byMi.keys()].sort((a, b) => a - b);
    const out: { pts: Tick[]; movement: Movement | undefined }[] = [];
    for (let i = 0; i < mis.length; i++) {
      const pts = byMi.get(mis[i])!.slice();
      const next = byMi.get(mis[i + 1]);
      if (next && next.length) pts.push(next[0]);
      out.push({ pts, movement: movements.find((mm) => mm.index === mis[i]) });
    }
    return out;
  });

  // Flat-colored movements: one polyline each, as before.
  const segments = $derived(
    grouped
      .filter((g) => !g.movement || !isRamsete(g.movement.type))
      .map((g) => ({
        points: g.pts
          .map((t) => {
            const p = toScreen(t.x, t.y, field);
            return `${p.sx},${p.sy}`;
          })
          .join(' '),
        color: g.movement ? movementColor(g.movement.type) : '#888888'
      }))
  );

  // RAMSETE movements: the driven path colored by the speed the chassis
  // actually carried — the same ramp the Trajectory Generator draws its plan
  // with, so the planned and the driven read alike.
  //
  // Drawn as RUNS of constant color, not one line per tick pair. A per-tick
  // line needs a cap at both ends, and where the robot is slow the ticks land
  // almost on top of each other, so those caps stack into a chain of beads that
  // reads as deliberate markers. Quantizing the ramp into STEPS bands lets a
  // whole stretch of path be one polyline: interior corners become round JOINS,
  // which don't bulge, and only the two ends of the whole run are capped.
  const STEPS = 14;

  const ramseteRuns = $derived.by(() => {
    const out: { points: string; color: string; end: boolean }[] = [];
    for (const g of grouped) {
      if (!g.movement || !isRamsete(g.movement.type)) continue;
      const speeds = g.pts.map((t) => ({ v: Math.abs(t.v) }));
      const { lo, hi } = speedRange(speeds, 60);

      // Band index for the midpoint speed of the pair ending at i.
      const band = (i: number) => {
        const v = (speeds[i - 1].v + speeds[i].v) / 2;
        const t = Math.max(0, Math.min(1, (v - lo) / Math.max(1e-6, hi - lo)));
        return Math.round(t * (STEPS - 1));
      };

      const first = out.length;
      let run: string[] = [];
      let cur = -1;
      for (let i = 1; i < g.pts.length; i++) {
        const bi = band(i);
        if (bi !== cur) {
          // Seed the new run with the previous point so the bands butt together
          // instead of leaving a gap at the colour change.
          if (run.length) out.push({ points: run.join(' '), color: speedColor(lo + ((hi - lo) * cur) / (STEPS - 1), lo, hi), end: false });
          const p = toScreen(g.pts[i - 1].x, g.pts[i - 1].y, field);
          run = [`${p.sx},${p.sy}`];
          cur = bi;
        }
        const b = toScreen(g.pts[i].x, g.pts[i].y, field);
        run.push(`${b.sx},${b.sy}`);
      }
      if (run.length) out.push({ points: run.join(' '), color: speedColor(lo + ((hi - lo) * cur) / (STEPS - 1), lo, hi), end: false });

      // Only the true start and finish of the movement get a round cap.
      if (out.length > first) {
        out[first].end = true;
        out[out.length - 1].end = true;
      }
    }
    return out;
  });
</script>

{#each segments as s}
  <polyline
    points={s.points}
    fill="none"
    stroke={s.color}
    stroke-width="1.4"
    stroke-linejoin="round"
    stroke-linecap="round"
    vector-effect="non-scaling-stroke"
  />
{/each}

{#if ramseteRuns.length}
  <!-- blurred underlay + bright core, the generator's treatment at about half
       its bloom: the replay field is already carrying overlays and a ghost, and
       the generator's glow buries them -->
  <defs>
    <!-- Region in field units, not %-of-bbox: a bbox-relative region is thin
         wherever the path is, so the blur hits the edge and cuts off square. -->
    <filter id="driven-glow" filterUnits="userSpaceOnUse" x="-24" y="-24" width="192" height="192">
      <feGaussianBlur stdDeviation="0.9" />
    </filter>
  </defs>
  <g class="glow" filter="url(#driven-glow)">
    {#each ramseteRuns as s}
      <polyline
        points={s.points}
        fill="none"
        stroke={s.color}
        stroke-width="2.4"
        stroke-linejoin="round"
        stroke-linecap={s.end ? 'round' : 'butt'}
      />
    {/each}
  </g>
  <g class="core">
    {#each ramseteRuns as s}
      <polyline
        points={s.points}
        fill="none"
        stroke={s.color}
        stroke-width="1.5"
        stroke-linejoin="round"
        stroke-linecap={s.end ? 'round' : 'butt'}
        vector-effect="non-scaling-stroke"
      />
    {/each}
  </g>
{/if}

<style>
  .glow {
    opacity: 0.22;
    pointer-events: none;
  }
  .core {
    pointer-events: none;
  }
</style>
