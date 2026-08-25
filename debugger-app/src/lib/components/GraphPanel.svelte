<script lang="ts">
  import { traceState } from '$lib/stores/trace.svelte';

  type Series = { data: { t: number; v: number }[]; color: string; dashed?: boolean };

  let {
    title,
    grow = 1,
    active = false,
    series = [],
    tStart = 0,
    tEnd = 1,
    yMin = -12,
    yMax = 12,
    yTicks = [12, 6, 0, -6, -12],
    playheadT = undefined,
    yLabel = 'V (±12)',
    accent = 'transparent',
    legend = [],
    onscrub = undefined
  }: {
    title: string;
    grow?: number;
    active?: boolean;
    series?: Series[];
    tStart?: number;
    tEnd?: number;
    yMin?: number;
    yMax?: number;
    yTicks?: number[];
    playheadT?: number;
    /** what the y-axis is measuring — volts unless the panel says otherwise */
    yLabel?: string;
    accent?: string;
    legend?: { label: string; color: string; dashed?: boolean }[];
    /** scrub target — defaults to the replay traceState; sandbox passes its own */
    onscrub?: (t: number) => void;
  } = $props();

  // where a drag-scrub lands: the injected handler, or the replay store
  function scrubTo(tv: number) {
    if (onscrub) onscrub(tv);
    else {
      traceState.playing = false;
      traceState.time = tv;
    }
  }

  const xTicks = [0, 0.25, 0.5, 0.75, 1];

  function sx(t: number): number {
    return tEnd > tStart ? ((t - tStart) / (tEnd - tStart)) * 100 : 0;
  }
  // Pinned to the plot edge rather than allowed off it: .data is overflow-visible
  // so an out-of-range sample would otherwise draw straight across the panels
  // above and below. Hovering still reads the sample's true value.
  function sy(v: number): number {
    const p = (1 - (v - yMin) / (yMax - yMin)) * 100;
    return p < 0 ? 0 : p > 100 ? 100 : p;
  }

  function pts(s: Series): string {
    return s.data.map((d) => `${sx(d.t).toFixed(2)},${sy(d.v).toFixed(2)}`).join(' ');
  }

  const playX = $derived(playheadT !== undefined ? sx(playheadT) : null);

  // ---- crosshair inspection + drag-to-scrub ----
  // Hover = read-only crosshair with per-series values (each PID component's
  // magnitude in breakdown mode). Press/drag = scrub the shared playhead.
  let frameEl: HTMLElement | undefined = $state();
  let hoverT = $state<number | null>(null);
  let hoverYPct = $state(50);
  let dragging = $state(false);

  /** Series value at time t (nearest sample, binary search). */
  function valueAt(s: Series, t: number): number | null {
    const d = s.data;
    if (d.length === 0) return null;
    if (t <= d[0].t) return d[0].v;
    if (t >= d[d.length - 1].t) return d[d.length - 1].v;
    let lo = 0;
    let hi = d.length - 1;
    while (hi - lo > 1) {
      const mid = (lo + hi) >> 1;
      if (d[mid].t <= t) lo = mid;
      else hi = mid;
    }
    return t - d[lo].t < d[hi].t - t ? d[lo].v : d[hi].v;
  }

  const hoverVals = $derived.by(() => {
    const t = hoverT;
    if (t === null) return [];
    return series.map((s, i) => ({
      label: legend[i]?.label ?? 'V',
      color: s.color,
      v: valueAt(s, t)
    }));
  });
  const hoverX = $derived(hoverT === null ? 0 : Math.max(0, Math.min(100, sx(hoverT))));

  function timeFromEvent(e: PointerEvent): number {
    const rect = frameEl!.getBoundingClientRect();
    const u = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
    hoverYPct = Math.max(0, Math.min(100, ((e.clientY - rect.top) / rect.height) * 100));
    return tStart + u * (tEnd - tStart);
  }

  function onMove(e: PointerEvent) {
    if (!frameEl) return;
    hoverT = timeFromEvent(e);
    if (dragging) scrubTo(hoverT);
  }
  function onDown(e: PointerEvent) {
    if (!frameEl) return;
    dragging = true;
    try {
      frameEl.setPointerCapture(e.pointerId);
    } catch {
      // no active pointer (synthetic events) — window-level up still ends the drag
    }
    hoverT = timeFromEvent(e);
    scrubTo(hoverT);
  }
  function onUp() {
    dragging = false;
  }
  function onLeave() {
    if (!dragging) hoverT = null;
  }
</script>

<section class="panel graph" style="flex: {grow}; border: 1px solid {accent}">
  <div class="title" class:active>
    {title}
  </div>

  {#if legend.length}
    <div class="legend">
      {#each legend as item}
        <span class="legend-item">
          <svg class="swatch" viewBox="0 0 12 4" preserveAspectRatio="none">
            <line x1="0" y1="2" x2="12" y2="2" stroke={item.color} stroke-width="1.5" stroke-dasharray={item.dashed ? '2.5 1.5' : 'none'} />
          </svg>
          <span class="legend-label">{item.label}</span>
        </span>
      {/each}
    </div>
  {/if}

  <div class="plot">
    <span class="y-axis-label">{yLabel}</span>

    <div
      class="frame"
      bind:this={frameEl}
      onpointermove={onMove}
      onpointerdown={onDown}
      onpointerup={onUp}
      onpointerleave={onLeave}
      role="slider"
      tabindex="-1"
      aria-label="{title} — drag to scrub"
      aria-valuenow={playheadT !== undefined ? Math.round(playheadT) : 0}
    >
      {#each yTicks as v, i}
        <div class="y-tick" style="top: {(i / (yTicks.length - 1)) * 100}%">
          <span class="y-num">{v}</span>
          <span class="gridline" class:zero={v === 0}></span>
        </div>
      {/each}

      <div class="x-axis">
        {#each xTicks as u}
          <span class="x-tick" style="left: {u * 100}%"></span>
        {/each}
      </div>

      <svg class="data" viewBox="0 0 100 100" preserveAspectRatio="none">
        {#each series as s}
          <polyline
            points={pts(s)}
            fill="none"
            stroke={s.color}
            stroke-width="1.3"
            stroke-linejoin="round"
            stroke-dasharray={s.dashed ? '3 2' : 'none'}
            vector-effect="non-scaling-stroke"
          />
        {/each}
        {#if playX !== null}
          <line x1={playX} y1="0" x2={playX} y2="100" class="playhead" vector-effect="non-scaling-stroke" />
        {/if}
      </svg>

      {#if hoverT !== null}
        <div class="xline" style="left: {hoverX}%"></div>
        {#each hoverVals as hv}
          {#if hv.v !== null}
            <div class="xdot" style="left: {hoverX}%; top: {sy(hv.v)}%; background: {hv.color}"></div>
          {/if}
        {/each}
        <div class="xtag" class:flip={hoverX > 68} style="left: {hoverX}%; top: {hoverYPct}%">
          <span class="xt-t">t {(hoverT / 1000).toFixed(2)} s</span>
          {#each hoverVals as hv}
            {#if hv.v !== null}
              <span class="xt-row">
                <span class="xt-swatch" style="background: {hv.color}"></span>
                <span class="xt-lbl">{hv.label}</span>
                <span class="xt-v">{hv.v >= 0 ? '+' : ''}{hv.v.toFixed(2)}{hoverVals.length === 1 ? ' V' : ''}</span>
              </span>
            {/if}
          {/each}
        </div>
      {/if}
    </div>

    <span class="x-axis-label">Time (ms)</span>
  </div>
</section>

<style>
  .graph {
    min-height: 0;
  }

  .title {
    position: absolute;
    top: 10px;
    left: 12px;
    z-index: 2;
    font-size: 10px;
    font-weight: 500;
    color: var(--text-muted);
  }
  .title.active {
    font-weight: 700;
    color: var(--text-primary);
  }
  .legend {
    position: absolute;
    top: 9px;
    right: 12px;
    z-index: 2;
    display: flex;
    gap: 8px;
  }
  .legend-item {
    display: flex;
    align-items: center;
    gap: 3px;
  }
  .swatch {
    width: 12px;
    height: 4px;
  }
  .legend-label {
    font-size: 8px;
    font-weight: 500;
    color: var(--text-dim);
  }

  .plot {
    position: absolute;
    inset: 28px 14px 14px 14px;
  }

  .frame {
    position: absolute;
    inset: 4px 4px 18px 30px;
    border-left: 1px solid var(--border-subtle);
    border-bottom: 1px solid var(--border-subtle);
    cursor: crosshair;
    touch-action: none;
    outline: none;
  }

  .y-tick {
    position: absolute;
    left: 0;
    width: 100%;
    transform: translateY(-50%);
    display: flex;
    align-items: center;
  }
  .y-num {
    position: absolute;
    right: calc(100% + 6px);
    font-size: 7px;
    font-weight: 400;
    color: var(--text-ghost);
    line-height: 1;
  }
  .gridline {
    flex: 1;
    height: 1px;
    background: var(--border-subtle);
    opacity: 0.6;
  }
  .gridline.zero {
    opacity: 1;
    background: var(--border-light);
  }

  .x-axis {
    position: absolute;
    left: 0;
    right: 0;
    bottom: 0;
    height: 0;
  }
  .x-tick {
    position: absolute;
    bottom: -3px;
    width: 1px;
    height: 3px;
    background: var(--border-subtle);
  }

  .data {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
    overflow: visible;
  }
  .playhead {
    stroke: var(--text-dim);
    stroke-width: 1;
    stroke-dasharray: 2 2;
    opacity: 0.55;
  }

  /* crosshair: vertical hairline + a dot riding each series + value tag */
  .xline {
    position: absolute;
    top: 0;
    bottom: 0;
    width: 1px;
    background: #777;
    opacity: 0.5;
    pointer-events: none;
  }
  .xdot {
    position: absolute;
    width: 7px;
    height: 7px;
    border-radius: 50%;
    transform: translate(-50%, -50%);
    box-shadow: 0 0 0 1.5px rgba(0, 0, 0, 0.55);
    pointer-events: none;
  }
  .xtag {
    position: absolute;
    z-index: 3;
    transform: translate(10px, -50%);
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding: 5px 8px;
    background: var(--bg-raised);
    border-radius: var(--radius-small);
    box-shadow:
      var(--ring),
      0 5px 16px rgba(0, 0, 0, 0.5);
    pointer-events: none;
    white-space: nowrap;
  }
  .xtag.flip {
    transform: translate(calc(-100% - 10px), -50%);
  }
  .xt-t {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 8px;
    color: var(--text-dim);
  }
  .xt-row {
    display: flex;
    align-items: center;
    gap: 5px;
  }
  .xt-swatch {
    width: 8px;
    height: 3px;
    border-radius: 1.5px;
  }
  .xt-lbl {
    font-size: 8.5px;
    font-weight: 600;
    color: var(--text-dim);
    min-width: 10px;
  }
  .xt-v {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9.5px;
    font-weight: 700;
    color: var(--text-primary);
  }

  .y-axis-label {
    position: absolute;
    left: -2px;
    top: 50%;
    transform: translateY(-50%) rotate(-90deg);
    transform-origin: center;
    font-size: 7px;
    font-weight: 400;
    color: var(--text-ghost);
    white-space: nowrap;
  }
  .x-axis-label {
    position: absolute;
    bottom: -2px;
    left: 30px;
    right: 0;
    text-align: center;
    font-size: 7px;
    font-weight: 400;
    color: var(--text-ghost);
  }
</style>
