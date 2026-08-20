<script lang="ts">
  // Scatter + fitted curve, in pixel space so the dots stay round (the replay
  // graphs stretch a 0–100 viewBox, which a scatter can't survive). Hovering
  // reads out the nearest sample and what the model wanted there.
  import type { Pt } from '$lib/velocity/fit';

  let {
    title,
    sub = '',
    points = [],
    curve = [],
    xLabel,
    yLabel,
    dotColor = '#22d3ee',
    lineColor = '#e8e8e8',
    guides = [],
    grow = 1,
    empty = 'No data'
  }: {
    title: string;
    sub?: string;
    points?: Pt[];
    curve?: Pt[];
    xLabel: string;
    yLabel: string;
    dotColor?: string;
    lineColor?: string;
    guides?: { x?: number; y?: number; label: string; color?: string }[];
    grow?: number;
    empty?: string;
  } = $props();

  let w = $state(0);
  let h = $state(0);
  const PAD = { l: 44, r: 14, t: 12, b: 24 };

  const dom = $derived.by(() => {
    const all = [...points, ...curve];
    if (!all.length) return { x0: 0, x1: 1, y0: 0, y1: 1 };
    let x0 = Infinity;
    let x1 = -Infinity;
    let y0 = Infinity;
    let y1 = -Infinity;
    for (const p of all) {
      if (p.x < x0) x0 = p.x;
      if (p.x > x1) x1 = p.x;
      if (p.y < y0) y0 = p.y;
      if (p.y > y1) y1 = p.y;
    }
    for (const g of guides) {
      if (g.x !== undefined) {
        x0 = Math.min(x0, g.x);
        x1 = Math.max(x1, g.x);
      }
      if (g.y !== undefined) {
        y0 = Math.min(y0, g.y);
        y1 = Math.max(y1, g.y);
      }
    }
    const padY = (y1 - y0 || 1) * 0.08;
    const padX = (x1 - x0 || 1) * 0.02;
    return { x0: x0 - padX, x1: x1 + padX, y0: y0 - padY, y1: y1 + padY };
  });

  /** 1-2-5 tick steps over a domain. */
  function ticks(min: number, max: number, count: number): { v: number; label: string }[] {
    if (!(max > min)) return [];
    const raw = (max - min) / count;
    const mag = Math.pow(10, Math.floor(Math.log10(raw)));
    const norm = raw / mag;
    const step = (norm < 1.5 ? 1 : norm < 3 ? 2 : norm < 7 ? 5 : 10) * mag;
    const dec = Math.max(0, -Math.floor(Math.log10(step) + 1e-9));
    const out: { v: number; label: string }[] = [];
    for (let v = Math.ceil(min / step) * step; v <= max + step * 1e-6; v += step) {
      const val = Math.abs(v) < step * 1e-9 ? 0 : v;
      out.push({ v: val, label: val.toFixed(dec) });
    }
    return out;
  }

  const xTicks = $derived(ticks(dom.x0, dom.x1, 5));
  const yTicks = $derived(ticks(dom.y0, dom.y1, 4));

  const iw = $derived(Math.max(1, w - PAD.l - PAD.r));
  const ih = $derived(Math.max(1, h - PAD.t - PAD.b));
  const sx = $derived((v: number) => PAD.l + ((v - dom.x0) / (dom.x1 - dom.x0 || 1)) * iw);
  const sy = $derived((v: number) => PAD.t + (1 - (v - dom.y0) / (dom.y1 - dom.y0 || 1)) * ih);

  const curvePath = $derived(curve.map((p, i) => `${i ? 'L' : 'M'}${sx(p.x).toFixed(1)},${sy(p.y).toFixed(1)}`).join(''));
  // one dot per sample gets dense on a 300-point capture — shrink it rather
  // than thin the data out, since the noise band is the thing worth seeing
  const dotR = $derived(points.length > 150 ? 1.3 : points.length > 60 ? 1.8 : 2.6);

  let frame: HTMLElement | undefined = $state();
  let hover = $state<{ pt: Pt; model: number | null; px: number; py: number } | null>(null);

  /** Model value at x, read off the drawn curve (linear between samples). */
  function curveAt(x: number): number | null {
    if (curve.length < 2) return null;
    if (x <= curve[0].x || x >= curve[curve.length - 1].x) return null;
    let i = 1;
    while (i < curve.length - 1 && curve[i].x < x) i++;
    const a = curve[i - 1];
    const b = curve[i];
    const u = b.x > a.x ? (x - a.x) / (b.x - a.x) : 0;
    return a.y + (b.y - a.y) * u;
  }

  function onMove(e: PointerEvent) {
    if (!frame || !points.length) return;
    const r = frame.getBoundingClientRect();
    const xv = dom.x0 + ((e.clientX - r.left - PAD.l) / iw) * (dom.x1 - dom.x0);
    let best = points[0];
    let bd = Infinity;
    for (const p of points) {
      const d = Math.abs(p.x - xv);
      if (d < bd) {
        bd = d;
        best = p;
      }
    }
    hover = { pt: best, model: curveAt(best.x), px: sx(best.x), py: sy(best.y) };
  }
</script>

<section class="panel chart" style="flex: {grow}">
  <div class="head">
    <span class="title">{title}</span>
    {#if sub}<span class="sub">{sub}</span>{/if}
    {#if hover}
      <span class="read">
        <span class="rv">{xLabel.split(' ')[0]} {hover.pt.x.toFixed(3)}</span>
        <span class="rv" style="color: {dotColor}">{hover.pt.y.toFixed(3)}</span>
        {#if hover.model !== null}
          <span class="rv fit">fit {hover.model.toFixed(3)}</span>
          <span class="rv res">Δ {(hover.pt.y - hover.model >= 0 ? '+' : '') + (hover.pt.y - hover.model).toFixed(3)}</span>
        {/if}
      </span>
    {/if}
  </div>

  <div
    class="frame"
    bind:this={frame}
    bind:clientWidth={w}
    bind:clientHeight={h}
    onpointermove={onMove}
    onpointerleave={() => (hover = null)}
    role="img"
    aria-label={title}
  >
    {#if !points.length}
      <div class="empty">{empty}</div>
    {:else}
      <svg class="data" width={w} height={h}>
        {#each yTicks as t}
          <line class="grid" x1={PAD.l} y1={sy(t.v)} x2={w - PAD.r} y2={sy(t.v)} class:zero={t.v === 0} />
          <text class="tick" x={PAD.l - 6} y={sy(t.v)} text-anchor="end" dominant-baseline="middle">{t.label}</text>
        {/each}
        {#each xTicks as t}
          <line class="grid vert" x1={sx(t.v)} y1={PAD.t} x2={sx(t.v)} y2={h - PAD.b} />
          <text class="tick" x={sx(t.v)} y={h - PAD.b + 13} text-anchor="middle">{t.label}</text>
        {/each}

        {#each guides as g, gi}
          {#if g.x !== undefined}
            <line class="guide" x1={sx(g.x)} y1={PAD.t} x2={sx(g.x)} y2={h - PAD.b} stroke={g.color ?? '#eab308'} />
            <!-- stagger, so two guides a few pixels apart don't print over each other -->
            <text class="glbl" x={sx(g.x) + 4} y={PAD.t + 9 + gi * 11} fill={g.color ?? '#eab308'}>{g.label}</text>
          {:else if g.y !== undefined}
            <line class="guide" x1={PAD.l} y1={sy(g.y)} x2={w - PAD.r} y2={sy(g.y)} stroke={g.color ?? '#eab308'} />
            <text class="glbl" x={w - PAD.r - 4} y={sy(g.y) - 4} text-anchor="end" fill={g.color ?? '#eab308'}>{g.label}</text>
          {/if}
        {/each}

        {#each points as p}
          <circle cx={sx(p.x)} cy={sy(p.y)} r={dotR} fill={dotColor} opacity="0.75" />
        {/each}

        {#if curve.length > 1}
          <path d={curvePath} fill="none" stroke={lineColor} stroke-width="1.6" stroke-linejoin="round" />
        {/if}

        {#if hover}
          <line class="cross" x1={hover.px} y1={PAD.t} x2={hover.px} y2={h - PAD.b} />
          <circle cx={hover.px} cy={hover.py} r="3.2" fill="none" stroke={dotColor} stroke-width="1.4" />
        {/if}
      </svg>
    {/if}

    <span class="ylbl">{yLabel}</span>
  </div>
  <span class="xlbl">{xLabel}</span>
</section>

<style>
  .chart {
    display: flex;
    flex-direction: column;
    padding: 10px 12px 6px;
    min-height: 0;
  }
  .head {
    display: flex;
    align-items: baseline;
    gap: 7px;
    flex-shrink: 0;
    min-height: 15px;
  }
  .title {
    font-size: 11px;
    font-weight: 700;
    color: var(--text-primary);
  }
  .sub {
    font-size: 8.5px;
    color: var(--text-ghost);
  }
  .read {
    margin-left: auto;
    display: flex;
    gap: 8px;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9px;
    font-variant-numeric: tabular-nums;
  }
  .rv {
    color: var(--text-dim);
  }
  .rv.fit {
    color: var(--text-secondary);
  }
  .rv.res {
    color: var(--text-ghost);
  }

  .frame {
    position: relative;
    flex: 1;
    min-height: 0;
    margin-top: 4px;
  }
  .empty {
    position: absolute;
    inset: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 9.5px;
    color: var(--text-ghost);
  }
  .data {
    display: block;
  }
  .grid {
    stroke: var(--border-subtle);
    stroke-width: 1;
  }
  .grid.vert {
    stroke: #1e1e1e;
  }
  .grid.zero {
    stroke: var(--border-light);
  }
  .tick {
    fill: var(--text-ghost);
    font-size: 8px;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
  }
  .guide {
    stroke-width: 1;
    stroke-dasharray: 3 3;
    opacity: 0.7;
  }
  /* halo, so a guide label stays legible wherever the data puts it */
  .glbl {
    font-size: 8px;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    opacity: 0.9;
    stroke: var(--bg-surface);
    stroke-width: 2.5px;
    paint-order: stroke;
  }
  .cross {
    stroke: var(--text-muted);
    stroke-width: 1;
    stroke-dasharray: 2 2;
  }

  .ylbl {
    position: absolute;
    top: 0;
    left: 0;
    font-size: 8px;
    font-weight: 700;
    letter-spacing: 0.04em;
    text-transform: uppercase;
    color: var(--text-ghost);
  }
  .xlbl {
    align-self: center;
    font-size: 8px;
    font-weight: 700;
    letter-spacing: 0.04em;
    text-transform: uppercase;
    color: var(--text-ghost);
    padding-bottom: 2px;
  }
</style>
