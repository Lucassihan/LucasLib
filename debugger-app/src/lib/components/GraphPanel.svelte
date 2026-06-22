<script lang="ts">
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
    accent = 'transparent',
    legend = []
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
    accent?: string;
    legend?: { label: string; color: string; dashed?: boolean }[];
  } = $props();

  const xTicks = [0, 0.25, 0.5, 0.75, 1];

  function sx(t: number): number {
    return tEnd > tStart ? ((t - tStart) / (tEnd - tStart)) * 100 : 0;
  }
  function sy(v: number): number {
    return (1 - (v - yMin) / (yMax - yMin)) * 100;
  }
  function pts(s: Series): string {
    return s.data.map((d) => `${sx(d.t).toFixed(2)},${sy(d.v).toFixed(2)}`).join(' ');
  }

  const playX = $derived(playheadT !== undefined ? sx(playheadT) : null);
</script>

<section class="panel graph" style="flex: {grow}; border: 1px solid {accent}">
  <div class="title" class:active>{title}</div>

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
    <span class="y-axis-label">V (±12)</span>

    <div class="frame">
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
