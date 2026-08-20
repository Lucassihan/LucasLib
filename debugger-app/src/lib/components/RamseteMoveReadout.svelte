<script lang="ts">
  // Steering readout for ramsete.moveTo, shown in place of the PID readout.
  // Splits the movement's two channels: the drive command v = cos(eθ)·drivePID(le),
  // and the turn ω = headingPID(eθ) + the lyapunov coupling kLat·v·sinc(eθ)·e_y.
  // The coupling is FAR-only — see $lib/trace/ramseteMove for how this differs
  // from the lyPose readout it replaces.
  import { traceState, movementAt, tickAt } from '$lib/stores/trace.svelte';
  import { settings } from '$lib/stores/settings.svelte';
  import { PID_TERM, COLORS, movementColor } from '$lib/trace/labels';
  import { moveToLaw } from '$lib/trace/ramseteMove';

  const m = $derived(movementAt(traceState.time));
  const tk = $derived(tickAt(traceState.time));

  const fmt = (n: number, d = 2) => n.toFixed(d);
  const signed = (n: number) => `${n >= 0 ? '+' : '−'}${fmt(Math.abs(n))}`;
  const barPct = (v: number) => Math.min(Math.abs(v) / 12, 1) * 100;

  const ACCENT = movementColor('ramsete.moveTo');
  const PROP = COLORS.rsProp;
  const LAT = COLORS.rsLat;

  const law = $derived(m && tk ? moveToLaw(m, tk, traceState.trace.header.kLat) : null);
  const pidSum = $derived(law ? law.dP + law.dI + law.dD : 0);
  // dimmed when there is nothing to show: CLOSE zeroes the term, and a trace
  // without #kLat can't split ω at all
  const latOff = $derived(!!law && (law.close || law.kLat < 0));
</script>

<section class="panel rs">
  <div class="head">
    <span class="lbl">RAMSETE point control</span>
    <span class="badge" style="background: {ACCENT}">{law && law.close ? 'CLOSE' : 'FAR'}</span>
    <span class="sub">point-chase</span>
    <button
      class="toggle"
      class:on={settings.showBreakdown}
      onclick={() => (settings.showBreakdown = !settings.showBreakdown)}
      aria-pressed={settings.showBreakdown}
    >
      <span class="tg-text">Breakdown</span>
      <span class="tg-track"><span class="tg-knob"></span></span>
    </button>
  </div>

  {#if law && m}
    <div class="errs">
      <div class="stat"><span class="sk">heading eθ</span><span class="sv">{fmt(law.heading, 1)}°</span></div>
      <div class="stat"><span class="sk">lateral e</span><span class="sv">{fmt(law.lateral)} in</span></div>
      <div class="stat" class:off={law.close}>
        <span class="sk">cross-track eᵧ</span>
        <span class="sv">{law.close ? '—' : `${fmt(law.crossTrack)} in`}</span>
      </div>
    </div>

    <div class="chan">
      <div class="cname">Drive PID&nbsp; <span class="cdim">on lateral e</span></div>
      <div class="term">
        <span class="tl" style="color: {PID_TERM.p}">P</span>
        <div class="bar"><div class="fill" style="width: {barPct(law.dP)}%; background: {PID_TERM.p}"></div></div>
        <span class="tv">{signed(law.dP)} V</span>
      </div>
      <div class="chips">
        <span class="chip">kP</span><span class="cv">{fmt(m.driveKp, 3)}</span>
        <span class="mul">×</span>
        <span class="chip">e</span><span class="cv">{fmt(law.lateral)}</span>
      </div>
      <div class="term">
        <span class="tl" style="color: {PID_TERM.i}">I</span>
        <div class="bar"><div class="fill" style="width: {barPct(law.dI)}%; background: {PID_TERM.i}"></div></div>
        <span class="tv">{signed(law.dI)} V</span>
      </div>
      <div class="term">
        <span class="tl" style="color: {PID_TERM.d}">D</span>
        <div class="bar"><div class="fill" style="width: {barPct(law.dD)}%; background: {PID_TERM.d}"></div></div>
        <span class="tv">{signed(law.dD)} V</span>
      </div>

      <!-- the cosine sits between the PID and the command, and it is SIGNED here
           (not |cos|), so a target that ends up behind the robot reverses -->
      <div class="scale">
        <span class="sum">Σ PID {signed(pidSum)} V</span>
        <span class="mul">×</span>
        <span class="chip">cos eθ</span><span class="cv">{fmt(law.cosFactor, 3)}</span>
      </div>
      <div class="outrow"><span class="olabel">v output</span><span class="oval">{signed(law.drivePow)} V</span></div>
    </div>

    <div class="chan">
      <div class="cname">Turn ω</div>
      <div class="term">
        <span class="tl" style="color: {PROP}">P</span>
        <div class="bar"><div class="fill" style="width: {barPct(law.propTerm)}%; background: {PROP}"></div></div>
        <span class="tv">{signed(law.propTerm)} V</span>
      </div>
      <div class="chips">
        <span class="chip">heading PID</span><span class="mul">·</span>
        <span class="chip">kP</span><span class="cv">{fmt(m.turnKp, 3)}</span>
        <span class="mul">×</span>
        <span class="chip">eθ</span><span class="cv">{fmt(law.heading, 1)}</span>
      </div>
      <div class="term" class:off={latOff}>
        <span class="tl" style="color: {latOff ? 'var(--text-ghost)' : LAT}">L</span>
        <div class="bar"><div class="fill" style="width: {barPct(law.latTerm)}%; background: {LAT}"></div></div>
        <span class="tv">{latOff ? '—' : `${signed(law.latTerm)} V`}</span>
      </div>
      <div class="chips" class:off={latOff}>
        {#if law.close}
          <span class="chip">coupling off inside headingDisableDist</span>
        {:else if law.kLat < 0}
          <span class="chip">kLat not in this trace</span>
        {:else}
          <span class="chip">kLat</span><span class="cv">{fmt(law.kLat, 3)}</span>
          <span class="mul">×</span>
          <span class="chip">v</span><span class="cv">{fmt(law.drivePow)}</span>
          <span class="mul">×</span>
          <span class="chip">eᵧ</span><span class="cv">{fmt(law.crossTrack)}</span>
          <span class="mul">×</span>
          <span class="chip">sinc</span><span class="cv">{fmt(law.sincv)}</span>
        {/if}
      </div>
      <div class="outrow"><span class="olabel">ω output</span><span class="oval">{signed(law.headingPow)} V</span></div>
    </div>
  {/if}
</section>

<style>
  .rs {
    flex: 0 0 auto;
    padding: 12px 14px;
    display: flex;
    flex-direction: column;
    gap: 10px;
  }
  .head {
    display: flex;
    align-items: center;
    gap: 6px;
  }
  .lbl {
    font-size: 10px;
    font-weight: 700;
    color: var(--text-primary);
  }
  .badge {
    font-size: 8px;
    font-weight: 700;
    color: #062a12;
    border-radius: 3px;
    padding: 1px 5px;
    letter-spacing: 0.04em;
  }
  .sub {
    font-size: 8px;
    color: var(--text-ghost);
  }
  .toggle {
    margin-left: auto;
    display: flex;
    align-items: center;
    gap: 6px;
    background: none;
    border: none;
    padding: 0;
    cursor: pointer;
  }
  .tg-text {
    font-size: 9px;
    font-weight: 500;
    color: var(--text-muted);
  }
  .toggle.on .tg-text {
    color: var(--text-secondary);
  }
  .tg-track {
    position: relative;
    width: 22px;
    height: 12px;
    border-radius: 6px;
    background: var(--bg-base);
    box-shadow: var(--ring);
    transition: background 0.15s ease;
  }
  .toggle.on .tg-track {
    background: #4ade80;
  }
  .tg-knob {
    position: absolute;
    top: 2px;
    left: 2px;
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: #999;
    transition:
      left 0.15s ease,
      background 0.15s ease;
  }
  .toggle.on .tg-knob {
    left: 12px;
    background: #fff;
  }
  .errs {
    display: flex;
    gap: 6px;
  }
  .stat {
    flex: 1;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding: 6px 8px;
    background: var(--bg-base);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
  }
  .stat.off .sv {
    color: var(--text-ghost);
  }
  .sk {
    font-size: 8px;
    color: var(--text-muted);
  }
  .sv {
    font-size: 11px;
    font-weight: 600;
    color: var(--text-primary);
  }
  .chan {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }
  .cname {
    font-size: 10px;
    font-weight: 600;
    color: var(--text-secondary);
  }
  .cdim {
    font-weight: 400;
    color: var(--text-ghost);
  }
  .term {
    display: flex;
    align-items: center;
    gap: 7px;
  }
  .term.off .bar .fill {
    background: var(--text-ghost) !important;
    opacity: 0.35;
  }
  .term.off .tv {
    color: var(--text-ghost);
  }
  .tl {
    width: 24px;
    font-size: 9px;
    font-weight: 700;
    text-align: center;
  }
  .bar {
    flex: 1;
    height: 6px;
    border-radius: 3px;
    background: var(--bg-base);
    box-shadow: var(--ring);
    overflow: hidden;
    min-width: 0;
  }
  .fill {
    height: 100%;
    border-radius: 3px;
  }
  .tv {
    width: 52px;
    text-align: right;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9.5px;
    font-weight: 600;
    color: var(--text-primary);
  }
  .chips {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 4px;
    margin: -1px 0 3px 31px;
  }
  .chips.off {
    opacity: 0.5;
  }
  .chip {
    font-size: 8px;
    padding: 1px 4px;
    border-radius: 3px;
    background: var(--bg-base);
    color: var(--text-muted);
    box-shadow: var(--ring);
  }
  .cv {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 8.5px;
    font-weight: 600;
    color: var(--text-primary);
  }
  .mul {
    font-size: 8px;
    color: var(--text-ghost);
  }
  .scale {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 4px;
    margin: 2px 0 0 31px;
  }
  .sum {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 8.5px;
    font-weight: 600;
    color: var(--text-muted);
  }
  .outrow {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    margin-top: 3px;
  }
  .olabel {
    font-size: 10px;
    font-weight: 600;
    color: var(--text-secondary);
  }
  .oval {
    font-size: 13px;
    font-weight: 700;
    color: var(--text-emphasis);
  }
</style>
