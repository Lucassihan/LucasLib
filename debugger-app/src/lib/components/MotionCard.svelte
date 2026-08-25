<script lang="ts">
  // What a trajectory follow actually did. Replaces the Governors card there: a trajectory drives
  // through the velocity layer, so the governor rails it would report are the same 0/12 V on every
  // move and say nothing about the follow.
  import { traceState, movementAt, movementTicks } from '$lib/stores/trace.svelte';
  import { motionStats } from '$lib/trace/motion';

  const m = $derived(movementAt(traceState.time));
  const s = $derived(motionStats(m ? movementTicks(m.index) : []));
</script>

<div class="card">
  <div class="head">
    <span class="group-label">Motion</span>
    <span class="scope">measured</span>
  </div>

  <div class="row"><span class="k">average V</span><span class="v">{s.avgV.toFixed(1)} in/s</span></div>
  <div class="row"><span class="k">average ω</span><span class="v">{s.avgW.toFixed(1)} °/s</span></div>

  <div class="peak">
    <div class="peak-label">Peak experienced</div>
    <div class="row"><span class="k">V</span><span class="v">{s.maxV.toFixed(1)} in/s</span></div>
    <div class="row"><span class="k">ω</span><span class="v">{s.maxW.toFixed(1)} °/s</span></div>
    <div class="row"><span class="k">accel</span><span class="v">{s.maxAccel.toFixed(0)} in/s²</span></div>
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
    align-items: baseline;
    gap: 8px;
    margin-bottom: 9px;
  }
  .group-label {
    font-size: 10px;
    font-weight: 700;
    color: var(--text-secondary);
    letter-spacing: 0.02em;
  }
  .scope {
    margin-left: auto;
    font-size: 9px;
    font-weight: 600;
    color: var(--text-muted);
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
