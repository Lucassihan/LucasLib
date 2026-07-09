<script lang="ts">
  // Lyapunov control readout — shown in place of the PID readout for lyPose
  // moves. Decomposes the path-follow law: drive v = |cos eθ|·v_d, and the turn
  // ω = heading-PID term + the Lyapunov lateral-coupling term kLat·v·e_y·sinc(eθ).
  import { traceState, movementAt, tickAt } from '$lib/stores/trace.svelte';
  import { settings } from '$lib/stores/settings.svelte';
  import { PID_TERM, COLORS } from '$lib/trace/labels';

  const m = $derived(movementAt(traceState.time));
  const tk = $derived(tickAt(traceState.time));

  const fmt = (n: number, d = 2) => n.toFixed(d);
  const signed = (n: number) => `${n >= 0 ? '+' : '−'}${fmt(Math.abs(n))}`;
  const barPct = (v: number) => Math.min(Math.abs(v) / 12, 1) * 100;
  const sinc = (x: number) => (Math.abs(x) < 1e-3 ? 1 - (x * x) / 6 : Math.sin(x) / x);

  const D2R = Math.PI / 180;
  const PROP = COLORS.lyP;
  const LATC = COLORS.lyLat;

  const law = $derived.by(() => {
    if (!m || !tk) return null;
    const kLat = traceState.trace.header.kLat;
    const eTheta = tk.headingError; // deg
    const eY = tk.ey;
    const eX = tk.ex;
    const v = tk.driveOut; // cosine-scaled forward, feeds the coupling term
    const omega = tk.turnOut;
    const sincv = sinc(eTheta * D2R);
    // Drive PID runs on e_x (along-line distance).
    const dP = m.driveKp * eX;
    const dI = m.driveKi * tk.driveI;
    const dD = m.driveKd * tk.driveD;
    const vd = tk.drivePow; // PID output
    // ω = −(headingPID(eθ) + kLat·v·e_y·sinc). Treat the whole heading-PID
    // output as the "proportional" term; the rest is the Lyapunov coupling.
    const latTerm = -kLat * v * eY * sincv;
    const propTerm = omega - latTerm;
    return { kLat, eTheta, eY, eX, v, omega, sincv, dP, dI, dD, vd, latTerm, propTerm };
  });
</script>

<section class="panel ly">
  <div class="head">
    <span class="lbl">Lyapunov control</span>
    <span class="badge">LYAP</span>
    <span class="sub">path-follow</span>
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

  {#if law}
    <div class="errs">
      <div class="stat"><span class="sk">heading eθ</span><span class="sv">{fmt(law.eTheta, 1)}°</span></div>
      <div class="stat"><span class="sk">cross-track eᵧ</span><span class="sv">{fmt(law.eY)} in</span></div>
      <div class="stat"><span class="sk">along-line eₓ</span><span class="sv">{fmt(law.eX)} in</span></div>
    </div>

    <div class="chan">
      <div class="cname">Drive PID&nbsp; <span class="cdim">on eₓ</span></div>
      <div class="term">
        <span class="tl" style="color: {PID_TERM.p}">P</span>
        <div class="bar"><div class="fill" style="width: {barPct(law.dP)}%; background: {PID_TERM.p}"></div></div>
        <span class="tv">{signed(law.dP)} V</span>
      </div>
      <div class="chips">
        <span class="chip">kP</span><span class="cv">{fmt(m.driveKp, 3)}</span>
        <span class="mul">×</span>
        <span class="chip">eₓ</span><span class="cv">{fmt(law.eX)}</span>
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
      <div class="outrow"><span class="olabel">v_d output</span><span class="oval">{signed(law.vd)} V</span></div>
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
        <span class="chip">eθ</span><span class="cv">{fmt(law.eTheta, 1)}</span>
      </div>
      <div class="term">
        <span class="tl" style="color: {LATC}">L</span>
        <div class="bar"><div class="fill" style="width: {barPct(law.latTerm)}%; background: {LATC}"></div></div>
        <span class="tv">{signed(law.latTerm)} V</span>
      </div>
      <div class="chips">
        <span class="chip">kLat</span><span class="cv">{fmt(law.kLat, 3)}</span>
        <span class="mul">×</span>
        <span class="chip">v</span><span class="cv">{fmt(law.v)}</span>
        <span class="mul">×</span>
        <span class="chip">eᵧ</span><span class="cv">{fmt(law.eY)}</span>
        <span class="mul">×</span>
        <span class="chip">sinc</span><span class="cv">{fmt(law.sincv)}</span>
      </div>
      <div class="outrow"><span class="olabel">ω output</span><span class="oval">{signed(law.omega)} V</span></div>
    </div>
  {/if}
</section>

<style>
  .ly {
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
    color: #0c2c2a;
    background: #2dd4bf;
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
    background: #2dd4bf;
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
