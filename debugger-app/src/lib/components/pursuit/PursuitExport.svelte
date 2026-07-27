<script lang="ts">
  // Right panel — the planned-speed profile over arc length (interactive: hover
  // to read (s, v), click/drag to scrub the field playhead), then the paste-ready
  // LucasLib snippet (PathPoint[] array + the followPath call).
  import { pursuit, viewOpts, playback, getPlan, exportCpp, speedColor, speedRange, planDuration } from '$lib/stores/pursuit.svelte';
  import SavedPaths from './SavedPaths.svelte';

  const plan = $derived(getPlan());
  const code = $derived(exportCpp());
  const duration = $derived(planDuration(plan.points));

  const UNITS: ViewUnit[] = ['in/s', 'ft/s', '%'];
  type ViewUnit = 'in/s' | 'ft/s' | '%';
  function fmtSpeed(v: number): string {
    if (viewOpts.unit === 'ft/s') return (v / 12).toFixed(1) + ' ft/s';
    if (viewOpts.unit === '%') return Math.round((v / Math.max(1, pursuit.params.maxVel)) * 100) + '%';
    return v.toFixed(0) + ' in/s';
  }

  // per-point cumulative time, aligned to plan.points, for playhead ↔ arc mapping
  const times = $derived.by(() => {
    const pts = plan.points;
    const out = [0];
    for (let i = 1; i < pts.length; i++) {
      const ds = pts[i].s - pts[i - 1].s;
      const vAvg = Math.max(2, (pts[i].v + pts[i - 1].v) / 2);
      out.push(out[i - 1] + ds / vAvg);
    }
    return out;
  });

  /** Arc length at playback time t. */
  function sAtT(t: number): number {
    const pts = plan.points;
    if (pts.length < 2 || t <= 0) return 0;
    let i = 1;
    while (i < times.length - 1 && times[i] < t) i++;
    const t0 = times[i - 1];
    const t1 = times[i];
    const u = t1 > t0 ? (t - t0) / (t1 - t0) : 0;
    return pts[i - 1].s + (pts[i].s - pts[i - 1].s) * u;
  }
  /** Playback time at arc length s. */
  function tAtS(s: number): number {
    const pts = plan.points;
    if (pts.length < 2 || s <= 0) return 0;
    let i = 1;
    while (i < pts.length - 1 && pts[i].s < s) i++;
    const s0 = pts[i - 1].s;
    const s1 = pts[i].s;
    const u = s1 > s0 ? (s - s0) / (s1 - s0) : 0;
    return times[i - 1] + (times[i] - times[i - 1]) * u;
  }
  /** Planned speed at arc length s. */
  function vAtS(s: number): number {
    const pts = plan.points;
    if (!pts.length) return 0;
    let i = 1;
    while (i < pts.length - 1 && pts[i].s < s) i++;
    const s0 = pts[i - 1].s;
    const s1 = pts[i].s;
    const u = s1 > s0 ? (s - s0) / (s1 - s0) : 0;
    return pts[i - 1].v + (pts[i].v - pts[i - 1].v) * u;
  }

  // velocity strip geometry (x = arc length, y = speed)
  const VW = 100;
  const VH = 30;
  const stripPts = $derived.by(() => {
    if (plan.points.length < 2 || plan.total < 1e-6) return [] as { x: number; y: number; color: string }[];
    const vmax = Math.max(1, pursuit.params.maxVel);
    const { lo, hi } = speedRange(plan.points, vmax);
    return plan.points.map((p) => ({
      x: (p.s / plan.total) * VW,
      y: VH - (p.v / vmax) * (VH - 2),
      color: speedColor(p.v, lo, hi)
    }));
  });
  const fillD = $derived.by(() => {
    if (!stripPts.length) return '';
    let d = `M 0 ${VH}`;
    for (const p of stripPts) d += ` L ${p.x.toFixed(2)} ${p.y.toFixed(2)}`;
    return d + ` L ${VW} ${VH} Z`;
  });

  // secondary overlay: curvature |κ| or path acceleration, normalized to its own
  // max, drawn faint over the speed fill
  const overlayD = $derived.by(() => {
    if (viewOpts.overlay === 'off') return '';
    const dense = plan.dense;
    if (dense.length < 2 || plan.total < 1e-6) return '';
    const vals: number[] = [];
    for (let i = 0; i < dense.length; i++) {
      if (viewOpts.overlay === 'k') vals.push(Math.abs(dense[i].k));
      else {
        const a = dense[Math.max(0, i - 1)];
        const b = dense[Math.min(dense.length - 1, i + 1)];
        const dv = b.v - a.v;
        const ds = b.s - a.s || 1;
        vals.push(Math.abs(dense[i].v * (dv / ds))); // a = v·dv/ds
      }
    }
    const mx = Math.max(1e-6, ...vals);
    let d = '';
    for (let i = 0; i < dense.length; i++) {
      const x = (dense[i].s / plan.total) * VW;
      const y = VH - (vals[i] / mx) * (VH - 2);
      d += (i ? ' L ' : 'M ') + x.toFixed(2) + ' ' + y.toFixed(2);
    }
    return d;
  });

  const playX = $derived(plan.total > 1e-6 ? (sAtT(playback.t) / plan.total) * VW : 0);

  // --- strip interaction: hover readout + click/drag to scrub -----------------
  let svgEl: SVGSVGElement | undefined = $state();
  let hover = $state<{ x: number; s: number; v: number; t: number } | null>(null);
  let scrubbing = $state(false);

  function sFromEvent(e: PointerEvent): number {
    if (!svgEl) return 0;
    const r = svgEl.getBoundingClientRect();
    const fx = Math.max(0, Math.min(1, (e.clientX - r.left) / r.width));
    return fx * plan.total;
  }
  function onMove(e: PointerEvent) {
    if (plan.total < 1e-6) return;
    const s = sFromEvent(e);
    hover = { x: (s / plan.total) * VW, s, v: vAtS(s), t: tAtS(s) };
    if (scrubbing) playback.t = hover.t;
  }
  function onDown(e: PointerEvent) {
    if (plan.total < 1e-6) return;
    scrubbing = true;
    playback.playing = false;
    playback.t = tAtS(sFromEvent(e));
    (e.currentTarget as SVGSVGElement).setPointerCapture?.(e.pointerId);
  }
  function onUp() {
    scrubbing = false;
  }

  function cycleUnit() {
    const i = UNITS.indexOf(viewOpts.unit);
    viewOpts.unit = UNITS[(i + 1) % UNITS.length];
  }
  function cycleOverlay() {
    const order = ['off', 'k', 'a'] as const;
    const i = order.indexOf(viewOpts.overlay);
    viewOpts.overlay = order[(i + 1) % order.length];
  }
  const overlayLabel = $derived(viewOpts.overlay === 'k' ? 'κ' : viewOpts.overlay === 'a' ? 'accel' : 'overlay');

  // minimal C++ colorizer — sandbox editor's Dark+ palette, comment/keyword/number
  const NUM_RE = new RegExp('(^|[({,=\\s])(-?\\d+\\.?\\d*)', 'g');
  function hl(src: string): string {
    return src
      .split('\n')
      .map((line) => {
        const esc = line.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
        if (esc.trim().startsWith('//')) return `<span class="tk-c">${esc}</span>`;
        return esc
          .replace(/\b(static|const)\b/g, '<span class="tk-k">$1</span>')
          .replace(/\b(std|vector|PathPoint)\b/g, '<span class="tk-t">$1</span>')
          .replace(/\b(followPath)\b/g, '<span class="tk-f">$1</span>')
          .replace(NUM_RE, '$1<span class="tk-n">$2</span>');
      })
      .join('\n');
  }
  const codeHtml = $derived(hl(code));

  let copied = $state(false);
  let copyTimer: ReturnType<typeof setTimeout> | undefined;
  async function copy() {
    try {
      await navigator.clipboard.writeText(code);
      copied = true;
      clearTimeout(copyTimer);
      copyTimer = setTimeout(() => (copied = false), 1400);
    } catch {
      // clipboard blocked — ignore
    }
  }
</script>

<div class="col">
  <section class="panel group">
    <div class="head-row">
      <div class="gtitle">Export</div>
      <input class="name" type="text" bind:value={pursuit.name} spellcheck="false" aria-label="Path name" />
    </div>

    <div class="statgrid">
      <div class="stat"><span class="slbl">time</span><span class="sval">{duration.toFixed(1)}<span class="sunit">s</span></span></div>
      <div class="stat"><span class="slbl">length</span><span class="sval">{plan.total.toFixed(1)}<span class="sunit">in</span></span></div>
      <div class="stat"><span class="slbl">points</span><span class="sval">{plan.points.length}</span></div>
    </div>

    <div class="vel">
      <svg
        bind:this={svgEl}
        viewBox="0 0 {VW} {VH}"
        preserveAspectRatio="none"
        class="vel-svg"
        role="slider"
        aria-label="Planned speed — click or drag to scrub"
        aria-valuenow={playback.t}
        tabindex="0"
        onpointerdown={onDown}
        onpointermove={onMove}
        onpointerup={onUp}
        onpointerleave={() => (hover = null)}
      >
        <path d={fillD} class="vel-fill" />
        {#if overlayD}
          <path d={overlayD} class="vel-overlay" vector-effect="non-scaling-stroke" />
        {/if}
        {#each stripPts as p, i}
          {#if i > 0}
            <line
              x1={stripPts[i - 1].x}
              y1={stripPts[i - 1].y}
              x2={p.x}
              y2={p.y}
              stroke={p.color}
              stroke-width="1"
              vector-effect="non-scaling-stroke"
              stroke-linecap="round"
            />
          {/if}
        {/each}
        {#if hover}
          <line x1={hover.x} y1="0" x2={hover.x} y2={VH} class="vel-hair" vector-effect="non-scaling-stroke" />
        {/if}
        {#if playback.t > 0}
          <line x1={playX} y1="0" x2={playX} y2={VH} class="vel-play" vector-effect="non-scaling-stroke" />
        {/if}
      </svg>
      <div class="vel-cap">
        {#if hover}
          <span class="vel-read">{hover.s.toFixed(0)}″ · {fmtSpeed(hover.v)} · {hover.t.toFixed(2)}s</span>
        {:else}
          <span>planned speed over arc length</span>
        {/if}
        <span class="vel-tools">
          <button class="vtool" onclick={cycleOverlay} title="Overlay curvature / acceleration">{overlayLabel}</button>
          <button class="vtool" onclick={cycleUnit} title="Speed units">{viewOpts.unit}</button>
        </span>
      </div>
    </div>
  </section>

  <SavedPaths />

  <section class="panel group code-group">
    <div class="head-row">
      <div class="gtitle">Snippet <span class="gsub">paste into auton.cpp</span></div>
      <button class="copy" class:done={copied} onclick={copy}>{copied ? 'Copied ✓' : 'Copy'}</button>
    </div>
    <pre class="code">{@html codeHtml}</pre>
  </section>
</div>

<style>
  .col {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: var(--gap);
    min-height: 0;
  }
  .group {
    padding: 12px 14px;
    display: flex;
    flex-direction: column;
    gap: 9px;
    flex-shrink: 0;
  }
  .gtitle {
    font-size: 11px;
    font-weight: 700;
    color: var(--text-primary);
    display: flex;
    align-items: baseline;
    gap: 6px;
  }
  .gsub {
    font-size: 8.5px;
    font-weight: 400;
    color: var(--text-ghost);
  }
  .head-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
  }

  .name {
    flex: 1;
    min-width: 0;
    max-width: 220px;
    background: none;
    border: none;
    outline: none;
    padding: 2px 0;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 12px;
    font-weight: 700;
    color: var(--text-primary);
    text-align: right;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
    transition: border-color 0.15s ease;
  }
  .name:hover {
    border-bottom-color: rgba(255, 255, 255, 0.18);
  }
  .name:focus {
    border-bottom-color: rgba(236, 216, 143, 0.55);
  }

  .statgrid {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 8px;
  }
  .stat {
    display: flex;
    flex-direction: column;
    gap: 3px;
    background: var(--bg-base);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 7px 10px 8px;
    min-width: 0;
  }
  .slbl {
    font-size: 8.5px;
    font-weight: 700;
    letter-spacing: 0.05em;
    text-transform: uppercase;
    color: var(--text-ghost);
  }
  .sval {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 15px;
    font-weight: 700;
    color: var(--text-primary);
    font-variant-numeric: tabular-nums;
    display: flex;
    align-items: baseline;
    gap: 3px;
  }
  .sunit {
    font-size: 9px;
    font-weight: 400;
    color: var(--text-dim);
  }

  .vel {
    display: flex;
    flex-direction: column;
    gap: 5px;
  }
  .vel-svg {
    width: 100%;
    height: 58px;
    display: block;
    background: var(--bg-base);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    cursor: ew-resize;
    touch-action: none;
    outline: none;
  }
  .vel-svg:focus-visible {
    box-shadow: var(--ring), 0 0 0 1px rgba(251, 191, 36, 0.4);
  }
  .vel-fill {
    fill: rgba(255, 255, 255, 0.04);
  }
  .vel-overlay {
    fill: none;
    stroke: rgba(96, 165, 250, 0.55);
    stroke-width: 0.8;
    stroke-dasharray: 2 1.5;
  }
  .vel-hair {
    stroke: rgba(255, 255, 255, 0.4);
    stroke-width: 0.7;
  }
  .vel-play {
    stroke: #fbbf24;
    stroke-width: 1;
  }
  .vel-cap {
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-size: 8.5px;
    color: var(--text-ghost);
    gap: 8px;
  }
  .vel-read {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    color: #fbbf24;
    font-variant-numeric: tabular-nums;
  }
  .vel-tools {
    display: flex;
    gap: 4px;
    flex-shrink: 0;
  }
  .vtool {
    padding: 2px 7px;
    border: none;
    border-radius: var(--radius-small);
    background: var(--bg-surface);
    box-shadow: var(--ring);
    color: var(--text-muted);
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 8.5px;
    font-weight: 600;
    cursor: pointer;
    transition: color 0.12s ease, background 0.12s ease;
  }
  .vtool:hover {
    color: var(--text-primary);
    background: var(--bg-raised);
  }

  .code-group {
    flex: 1;
    min-height: 0;
  }
  .copy {
    font-family: inherit;
    font-size: 10px;
    font-weight: 600;
    color: var(--text-secondary);
    background: var(--bg-base);
    border: none;
    box-shadow: var(--ring);
    border-radius: var(--radius-small);
    padding: 4px 14px;
    cursor: pointer;
    transition: color 0.15s ease, background 0.15s ease;
  }
  .copy:hover {
    color: var(--text-primary);
    background: var(--bg-raised);
  }
  .copy.done {
    color: #3ec97e;
  }

  .code {
    flex: 1;
    min-height: 0;
    overflow: auto;
    margin: 0;
    padding: 10px 12px;
    background: var(--bg-base);
    box-shadow: var(--ring);
    border-radius: var(--radius-small);
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 10.5px;
    line-height: 1.55;
    color: #d4d4d4;
    white-space: pre;
    tab-size: 2;
  }
  .code :global(.tk-c) {
    color: #6a9955;
    font-style: italic;
  }
  .code :global(.tk-k) {
    color: #569cd6;
  }
  .code :global(.tk-t) {
    color: #4ec9b0;
  }
  .code :global(.tk-f) {
    color: #dcdcaa;
  }
  .code :global(.tk-n) {
    color: #b5cea8;
  }
</style>
