<script lang="ts">
  // Output-governor settings for the scope selected by the toggle (shared with
  // the pipeline's activation indicators via settings.governScope). Shows the
  // configured limits plus the peak ramp rates actually experienced this move.
  import { traceState, movementAt, movementTicks } from '$lib/stores/trace.svelte';
  import { settings, type GovernScope } from '$lib/stores/settings.svelte';
  import { governConfig, experiencedRates } from '$lib/trace/governor';

  const scopes: { id: GovernScope; label: string }[] = [
    { id: 'total', label: 'Total' },
    { id: 'drive', label: 'Drive' },
    { id: 'turn', label: 'Turn' }
  ];

  const cfg = $derived(governConfig(traceState.trace.header, settings.governScope));
  const m = $derived(movementAt(traceState.time));
  const rates = $derived(
    m ? experiencedRates(movementTicks(m.index), settings.governScope) : { accel: 0, decel: 0 }
  );

  const rate = (v: number) => (v > 0 ? `${v.toFixed(2)} V/tick` : 'off');
  // peak vs limit: flag if the experienced rate met/exceeded the configured cap
  const accelHot = $derived(cfg.maxAccelRate > 0 && rates.accel >= cfg.maxAccelRate - 0.01);
  const decelHot = $derived(cfg.maxDecelRate > 0 && rates.decel >= cfg.maxDecelRate - 0.01);
</script>

<div class="card">
  <div class="head">
    <span class="group-label">Governors</span>
    <div class="seg">
      {#each scopes as s}
        <button
          class="seg-btn"
          class:active={settings.governScope === s.id}
          onclick={() => (settings.governScope = s.id)}
        >{s.label}</button>
      {/each}
    </div>
  </div>

  <div class="row"><span class="k">min output</span><span class="v">{cfg.minOutput.toFixed(1)} V</span></div>
  <div class="row"><span class="k">max output</span><span class="v">{cfg.maxOutput.toFixed(1)} V</span></div>
  <div class="row"><span class="k">max accel</span><span class="v">{rate(cfg.maxAccelRate)}</span></div>
  <div class="row"><span class="k">max decel</span><span class="v">{rate(cfg.maxDecelRate)}</span></div>

  <div class="peak">
    <div class="peak-label">Peak experienced</div>
    <div class="row"><span class="k">accel</span><span class="v" class:hot={accelHot}>{rates.accel.toFixed(2)} V/tick</span></div>
    <div class="row"><span class="k">decel</span><span class="v" class:hot={decelHot}>{rates.decel.toFixed(2)} V/tick</span></div>
  </div>
</div>

<style>
  .card {
    background: var(--bg-elevated);
    border-radius: 7px;
    padding: 11px 12px;
    box-shadow: var(--ring);
  }
  .head {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 9px;
  }
  .group-label {
    font-size: 10px;
    font-weight: 700;
    color: var(--text-secondary);
    letter-spacing: 0.02em;
  }
  .seg {
    margin-left: auto;
    display: flex;
    gap: 3px;
  }
  .seg-btn {
    background: var(--bg-base);
    color: var(--text-muted);
    border: none;
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 3px 8px;
    font-family: inherit;
    font-size: 9px;
    font-weight: 600;
    cursor: pointer;
    transition:
      background 0.15s ease,
      color 0.15s ease;
  }
  .seg-btn:hover {
    color: var(--text-primary);
  }
  .seg-btn.active {
    background: rgba(255, 255, 255, 0.12);
    color: var(--text-emphasis);
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.18);
  }
  .row {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    gap: 10px;
    margin: 4px 0;
  }
  .k {
    font-size: 10px;
    font-weight: 400;
    color: var(--text-dim);
  }
  .v {
    font-size: 10px;
    font-weight: 600;
    color: var(--text-primary);
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
  }
  .v.hot {
    color: #f87171;
  }
  .peak {
    margin-top: 9px;
    padding: 8px 10px;
    border-radius: var(--radius-small);
    background: var(--bg-base);
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.03);
  }
  .peak-label {
    font-size: 8px;
    font-weight: 600;
    color: var(--text-muted);
    margin-bottom: 4px;
  }
</style>
