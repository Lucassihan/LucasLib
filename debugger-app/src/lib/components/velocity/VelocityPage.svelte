<script lang="ts">
  // The whole tab: paste the capture, pick a trial, read the gains.
  // Single column, no panels — the five numbers are the product, everything
  // else is small enough to ignore until you want it.
  import FitChart from './FitChart.svelte';
  import { velocity, getFit, pickTrial, fmtGain, exportCpp, SET_CONSTANTS_PARAMS } from '$lib/stores/velocity.svelte';
  import { fopdtAt, type Pt } from '$lib/velocity/fit';

  const fit = $derived(getFit());
  const g = $derived(fit.gains);
  const f = $derived(fit.picked?.fopdt ?? null);

  const GAINS = $derived([
    { k: 'kV', v: g.kV, c: '#22d3ee' },
    { k: 'kA', v: g.kA, c: '#a78bfa' },
    { k: 'kS', v: g.kS, c: '#eab308' },
    { k: 'kP', v: g.kP, c: '#3ec97e' },
    { k: 'kI', v: g.kI, c: '#f97316' }
  ]);

  const sweepCurve = $derived.by(() => {
    const s = fit.steady;
    if (!s.ok || Math.abs(s.kV) < 1e-9 || !fit.sweep.length) return [];
    const xs = fit.sweep.map((p) => p.x);
    const lo = Math.min(0, ...xs);
    const hi = Math.max(0, ...xs);
    const out: Pt[] = [];
    for (let i = 0; i <= 120; i++) {
      const u = lo + ((hi - lo) * i) / 120;
      out.push({ x: u, y: Math.abs(u) <= Math.abs(s.kS) ? 0 : (u - Math.sign(u) * s.kS) / s.kV });
    }
    return out;
  });

  const stepPoints = $derived(fit.capture.trials[velocity.selected]?.samples ?? []);
  const stepCurve = $derived.by(() => {
    if (!f || !f.ok || stepPoints.length < 2 || !fit.picked) return [];
    const t0 = stepPoints[0].x;
    const t1 = stepPoints[stepPoints.length - 1].x;
    const out: Pt[] = [];
    for (let i = 0; i <= 160; i++) {
      const t = t0 + ((t1 - t0) * i) / 160;
      out.push({ x: t, y: fopdtAt(f, fit.picked.volts, t) });
    }
    return out;
  });

  const code = $derived(exportCpp());
  let copied = $state(false);
  let copyTimer: ReturnType<typeof setTimeout> | undefined;
  async function copy() {
    try {
      await navigator.clipboard.writeText(code);
      copied = true;
      clearTimeout(copyTimer);
      copyTimer = setTimeout(() => (copied = false), 1400);
    } catch {
      // clipboard blocked — the text is selectable
    }
  }
</script>

<div class="page">
  <div class="top">
    <textarea
      class="paste"
      spellcheck="false"
      placeholder="paste lin.txt / ang.txt"
      value={velocity.text}
      oninput={(e) => (velocity.text = e.currentTarget.value)}
    ></textarea>
  </div>

  <div class="gains">
    {#each GAINS as gn}
      <div class="g">
        <span class="gk" style="color: {gn.c}">{gn.k}</span>
        <span class="gv">{fmtGain(gn.v)}</span>
      </div>
    {/each}
  </div>

  <div class="controls">
    <div class="trials">
      {#each fit.trials as t, i}
        <button
          class="tv"
          class:on={i === velocity.selected}
          class:bad={t.drift > fit.capture.drift && fit.capture.drift > 0}
          onclick={() => pickTrial(i)}
          title="{t.speed.toFixed(2)} {fit.unit} · drift {t.drift.toFixed(3)}"
        >
          {t.volts.toFixed(1)}
        </button>
      {/each}
    </div>

    <label class="lam">
      <span>λ</span>
      <input
        type="range"
        min="0.1"
        max="2"
        step="0.05"
        value={velocity.lambdaFactor}
        oninput={(e) => (velocity.lambdaFactor = parseFloat(e.currentTarget.value))}
      />
      <span class="lamv">{velocity.lambdaFactor.toFixed(2)}·T</span>
    </label>
  </div>

  <div class="charts">
    <FitChart
      title="sweep"
      sub={fit.steady.ok ? `R² ${fit.steady.r2.toFixed(4)}` : ''}
      points={fit.sweep}
      curve={sweepCurve}
      xLabel="volts"
      yLabel={fit.unit}
      dotColor="#22d3ee"
      empty="—"
    />
    <FitChart
      title="step"
      sub={f && f.ok ? `K ${f.K.toFixed(3)} · L ${f.L.toFixed(3)} · T ${f.T.toFixed(3)} · R² ${f.r2.toFixed(3)}` : ''}
      points={stepPoints}
      curve={stepCurve}
      xLabel="seconds"
      yLabel={fit.unit}
      dotColor="#3ec97e"
      empty="—"
    />
  </div>

  <div class="foot">
    <code class="snip">
      <span class="tk-t">{fit.instance}</span>.<span class="tk-f">setConstants</span>(<!--
      -->{#each GAINS as gn, i}{i ? ', ' : ''}<span class="inlay">{SET_CONSTANTS_PARAMS[i]}:</span><span class="tk-n">{fmtGain(gn.v)}</span>{/each});
    </code>
    <button class="copy" class:done={copied} onclick={copy}>{copied ? '✓' : 'copy'}</button>
  </div>
</div>

<style>
  .page {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 14px;
    min-height: 0;
    padding: 4px 6px;
  }

  .top {
    display: flex;
    gap: 8px;
    align-items: flex-start;
    flex-shrink: 0;
  }
  .paste {
    flex: 1;
    height: 42px;
    resize: vertical;
    background: var(--bg-surface);
    border: none;
    box-shadow: var(--ring);
    border-radius: var(--radius-small);
    color: var(--text-secondary);
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 10px;
    line-height: 1.5;
    padding: 8px 10px;
    outline: none;
    user-select: text;
  }
  .paste::placeholder {
    color: var(--text-ghost);
  }

  /* the product: five numbers, as big as the column allows */
  .gains {
    display: grid;
    grid-template-columns: repeat(5, 1fr);
    gap: 18px;
    flex-shrink: 0;
  }
  .g {
    display: flex;
    flex-direction: column;
    gap: 1px;
    min-width: 0;
  }
  .gk {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 12px;
    font-weight: 700;
  }
  .gv {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: clamp(22px, 3.2vw, 40px);
    font-weight: 700;
    color: var(--text-emphasis);
    font-variant-numeric: tabular-nums;
    letter-spacing: -0.02em;
    line-height: 1.05;
  }

  .controls {
    display: flex;
    align-items: center;
    gap: 16px;
    flex-wrap: wrap;
    flex-shrink: 0;
  }
  .trials {
    display: flex;
    gap: 4px;
    flex-wrap: wrap;
  }
  .tv {
    background: var(--bg-surface);
    border: none;
    box-shadow: var(--ring);
    border-radius: var(--radius-small);
    color: var(--text-muted);
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 11px;
    font-variant-numeric: tabular-nums;
    padding: 5px 10px;
    cursor: pointer;
    transition: background 0.12s ease, color 0.12s ease;
  }
  .tv:hover {
    color: var(--text-primary);
  }
  .tv.on {
    background: #22d3ee;
    color: var(--bg-deep);
  }
  .tv.bad {
    color: #eab308;
  }

  .lam {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-left: auto;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 11px;
    color: var(--text-muted);
  }
  .lam input {
    width: 110px;
    appearance: none;
    -webkit-appearance: none;
    height: 4px;
    border-radius: 2px;
    background: var(--bg-surface);
    box-shadow: var(--ring);
    outline: none;
    cursor: pointer;
  }
  .lam input::-webkit-slider-thumb {
    appearance: none;
    -webkit-appearance: none;
    width: 11px;
    height: 11px;
    border-radius: 50%;
    background: #cfcfcf;
  }
  .lamv {
    color: var(--text-dim);
    font-variant-numeric: tabular-nums;
    min-width: 48px;
  }

  /* informative, but they take the slack rather than dominating */
  .charts {
    display: flex;
    gap: 10px;
    flex: 1 1 auto;
    min-height: 170px;
  }
  .charts :global(.chart) {
    flex: 1;
    min-width: 0;
  }

  .foot {
    display: flex;
    align-items: stretch;
    gap: 10px;
    flex-shrink: 0;
    margin-top: auto;
    padding-bottom: 4px;
  }
  /* the thing you came here to copy — the one raised surface on the page */
  .snip {
    flex: 1;
    min-width: 0;
    background: var(--bg-elevated);
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.09);
    border-radius: var(--radius-panel);
    padding: 15px 18px;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 19px;
    color: #e8e8e8;
    white-space: nowrap;
    overflow-x: auto;
    user-select: text;
  }
  .snip :global(.tk-t),
  .snip .tk-t {
    color: #4ec9b0;
  }
  .snip .tk-f {
    color: #dcdcaa;
  }
  .snip .tk-n {
    color: #b5cea8;
  }
  .snip .inlay {
    color: #9a9a9a;
    background: rgba(255, 255, 255, 0.07);
    border-radius: 5px;
    padding: 1px 5px;
    margin-right: 2px;
    font-size: 15px;
    user-select: none;
  }
  .copy {
    background: #22d3ee;
    border: none;
    border-radius: var(--radius-panel);
    color: var(--bg-deep);
    font-family: inherit;
    font-size: 14px;
    font-weight: 700;
    padding: 15px 26px;
    cursor: pointer;
    transition: background 0.12s ease;
  }
  .copy:hover {
    background: #67e3f5;
  }
  .copy.done {
    background: #3ec97e;
  }
</style>
