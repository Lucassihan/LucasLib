<script lang="ts">
  // Law readout for ramsete.trajectory, shown in place of the PID readout.
  //
  // A trajectory has no PID to break down — RAMSETE turns the gap between the
  // robot and the schedule's reference pose into a (v, ω) pair, and the velocity
  // layer turns that pair into volts. So this shows the gap, the command it
  // produced, and what the drive actually did with it.
  import { traceState, tickAt } from '$lib/stores/trace.svelte';
  import { movementColor } from '$lib/trace/labels';

  const tk = $derived(tickAt(traceState.time));

  const ACCENT = movementColor('ramsete.trajectory');
  const CMD = '#22d3ee';
  const MEAS = '#e8e8e8';

  const fmt = (n: number, d = 2) => (n >= 0 ? '+' : '−') + Math.abs(n).toFixed(d);

  // The clock scales reference time when the robot falls behind: 1.00 is
  // on-schedule, below that means the schedule is being held back to let the
  // drive catch up.
  const rate = $derived(tk?.clockRate ?? 0);
</script>

<section class="panel rs">
  <div class="head">
    <span class="lbl">RAMSETE trajectory</span>
    <span class="badge" style="background: {ACCENT}">SCHEDULE</span>
    {#if rate > 0}
      <span class="sub">clock ×{rate.toFixed(2)}</span>
    {/if}
  </div>

  {#if tk}
    <div class="errs">
      <div class="stat">
        <span class="sk">forward e</span>
        <span class="sv">{fmt(tk.errFwd)}<u>in</u></span>
      </div>
      <div class="stat">
        <span class="sk">cross-track e</span>
        <span class="sv">{fmt(tk.errCross)}<u>in</u></span>
      </div>
      <div class="stat">
        <span class="sk">heading e</span>
        <span class="sv">{fmt(tk.errHead, 1)}<u>°</u></span>
      </div>
    </div>

    <!-- commanded vs measured v and ω are the Chassis v / Chassis ω panels
         below, plotted over the whole run instead of one instant -->

    <div class="outs">
      <div class="stat">
        <span class="sk">V controller</span>
        <span class="sv">{fmt(tk.drivePow)}<u>V</u></span>
      </div>
      <div class="stat">
        <span class="sk">ω controller</span>
        <span class="sv">{fmt(tk.headingPow)}<u>V</u></span>
      </div>
    </div>
  {/if}
</section>

<style>
  .rs {
    padding: 12px 14px;
    display: flex;
    flex-direction: column;
    gap: 10px;
    flex-shrink: 0;
  }
  .head {
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .lbl {
    font-size: 11px;
    font-weight: 700;
    color: var(--text-primary);
  }
  .badge {
    font-size: 8px;
    font-weight: 700;
    letter-spacing: 0.06em;
    color: var(--bg-deep);
    padding: 2px 6px;
    border-radius: 4px;
  }
  .sub {
    margin-left: auto;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9px;
    color: var(--text-ghost);
  }

  .errs,
  .outs {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 8px;
  }
  .outs {
    grid-template-columns: repeat(2, 1fr);
  }
  .stat {
    display: flex;
    flex-direction: column;
    gap: 3px;
    background: var(--bg-base);
    box-shadow: var(--ring);
    border-radius: var(--radius-small);
    padding: 7px 10px 8px;
    min-width: 0;
  }
  .sk {
    font-size: 8px;
    font-weight: 700;
    letter-spacing: 0.05em;
    text-transform: uppercase;
    color: var(--text-ghost);
  }
  .sv {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 14px;
    font-weight: 700;
    color: var(--text-primary);
    font-variant-numeric: tabular-nums;
  }
  .sv u {
    text-decoration: none;
    font-size: 8.5px;
    font-weight: 400;
    color: var(--text-ghost);
    margin-left: 2px;
  }

  .chan {
    display: flex;
    align-items: baseline;
    gap: 10px;
  }
  .cname {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 11px;
    font-weight: 700;
    color: var(--text-secondary);
    width: 54px;
    flex-shrink: 0;
  }
  .cdim {
    font-size: 8px;
    font-weight: 400;
    color: var(--text-ghost);
  }
  .pair {
    display: flex;
    gap: 12px;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 10.5px;
    font-variant-numeric: tabular-nums;
  }
  .gap {
    color: var(--text-ghost);
  }
</style>
