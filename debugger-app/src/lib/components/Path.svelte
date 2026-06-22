<script lang="ts">
  import { toScreen, defaultField, type FieldConfig } from '../field/coords';
  import { movementColor } from '$lib/trace/labels';
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

  // The actual path is the recorded odometry points connected — one polyline
  // per movement, colored by movement type. Each segment borrows the next
  // movement's first point so the colored runs join without a gap.
  const segments = $derived.by(() => {
    const byMi = new Map<number, Tick[]>();
    for (const t of ticks) {
      const arr = byMi.get(t.mi);
      if (arr) arr.push(t);
      else byMi.set(t.mi, [t]);
    }

    const mis = [...byMi.keys()].sort((a, b) => a - b);
    const out: { points: string; color: string }[] = [];

    for (let i = 0; i < mis.length; i++) {
      const pts = byMi.get(mis[i])!.slice();
      const next = byMi.get(mis[i + 1]);
      if (next && next.length) pts.push(next[0]);

      const m = movements.find((mm) => mm.index === mis[i]);
      const color = m ? movementColor(m.type) : '#888888';
      const points = pts
        .map((t) => {
          const p = toScreen(t.x, t.y, field);
          return `${p.sx},${p.sy}`;
        })
        .join(' ');
      out.push({ points, color });
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
