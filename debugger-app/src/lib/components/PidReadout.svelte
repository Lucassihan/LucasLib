<script lang="ts">
  import { traceState, movementAt, tickAt } from '$lib/stores/trace.svelte';
  import { settings } from '$lib/stores/settings.svelte';
  import { PID_TERM } from '$lib/trace/labels';
  const m = $derived(movementAt(traceState.time));
  const tk = $derived(tickAt(traceState.time));

  const fmt = (n: number, d = 2) => n.toFixed(d);
  const signed = (n: number) => `${n >= 0 ? '+' : '−'}${fmt(Math.abs(n))}`;

  function turnLabel(type: string): string {
    if (type === 'face' || type === 'facePoint') return 'Turn PID';
    if (type.startsWith('swing')) return 'Swing PID';
    return 'Heading PID';
  }

  type Chan = {
    label: string;
    unit: string; // error unit shown in the header
    kp: number; ki: number; kd: number;
    e: number; ie: number; de: number;
    termP: number; termI: number; termD: number;
    output: number;
  };

  const channels = $derived.by<Chan[]>(() => {
    if (!m || !tk) return [];
    const out: Chan[] = [];
    if (m.driveKp !== 0 || m.driveKi !== 0 || m.driveKd !== 0) {
      const p = m.driveKp * tk.lateralError;
      const i = m.driveKi * tk.driveI;
      const d = m.driveKd * tk.driveD;
      out.push({
        label: 'Drive PID', unit: 'in',
        kp: m.driveKp, ki: m.driveKi, kd: m.driveKd,
        e: tk.lateralError, ie: tk.driveI, de: tk.driveD,
        termP: p, termI: i, termD: d, output: p + i + d
      });
    }
    {
      const p = m.turnKp * tk.headingError;
      const i = m.turnKi * tk.turnI;
      const d = m.turnKd * tk.turnD;
      out.push({
        label: turnLabel(m.type), unit: '°',
        kp: m.turnKp, ki: m.turnKi, kd: m.turnKd,
        e: tk.headingError, ie: tk.turnI, de: tk.turnD,
        termP: p, termI: i, termD: d, output: p + i + d
      });
    }
    return out;
  });

  const cols = $derived(Math.min(channels.length, 2));
  const errText = (c: Chan) => `${fmt(c.e, c.unit === '°' ? 1 : 2)}${c.unit === '°' ? '°' : ' in'}`;

  // Fixed 0–12 V scale: the bar track is always 12 V long, fill is proportional
  // and capped at 12 V (the actual value is still shown numerically).
  const barPct = (v: number) => Math.min(Math.abs(v) / 12, 1) * 100;
</script>

<section class="panel pid">
  <div class="head">
    <span class="lbl">PID readout</span>
    <span class="sub">at cursor</span>
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

  {#if m && tk}
    <div class="grid" style="grid-template-columns: repeat({cols}, minmax(0, 1fr));">
      {#each channels as c}
        <div class="chan">
          <div class="chead">
            <span class="cname">{c.label}</span>
            <span class="cerr">err {errText(c)}</span>
          </div>

          <div class="term">
            <span class="tl" style="color: {PID_TERM.p}">P</span>
            <div class="bar"><div class="fill" style="width: {barPct(c.termP)}%; background: {PID_TERM.p}"></div></div>
            <span class="tv">{signed(c.termP)} V</span>
          </div>
          <div class="chips">
            <span class="chip">kP</span><span class="chip-val">{fmt(c.kp, 3)}</span>
            <span class="chip-mul">×</span>
            <span class="chip">e</span><span class="chip-val">{fmt(c.e)}</span>
          </div>

          <div class="term">
            <span class="tl" style="color: {PID_TERM.i}">I</span>
            <div class="bar"><div class="fill" style="width: {barPct(c.termI)}%; background: {PID_TERM.i}"></div></div>
            <span class="tv">{signed(c.termI)} V</span>
          </div>
          <div class="chips">
            <span class="chip">kI</span><span class="chip-val">{fmt(c.ki, 3)}</span>
            <span class="chip-mul">×</span>
            <span class="chip">∫e</span><span class="chip-val">{fmt(c.ie)}</span>
          </div>

          <div class="term">
            <span class="tl" style="color: {PID_TERM.d}">D</span>
            <div class="bar"><div class="fill" style="width: {barPct(c.termD)}%; background: {PID_TERM.d}"></div></div>
            <span class="tv">{signed(c.termD)} V</span>
          </div>
          <div class="chips">
            <span class="chip">kD</span><span class="chip-val">{fmt(c.kd, 3)}</span>
            <span class="chip-mul">×</span>
            <span class="chip">Δe</span><span class="chip-val">{fmt(c.de)}</span>
          </div>

          <div class="outrow"><span class="olabel">Output</span><span class="oval">{fmt(c.output)} V</span></div>
        </div>
      {/each}
    </div>
  {/if}
</section>

<style>
  .pid {
    flex: 0 0 auto;
    padding: 12px 14px;
    display: flex;
    flex-direction: column;
    gap: 11px;
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
    background: #3b82f6;
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

  .grid {
    display: grid;
    gap: 12px;
  }
  .chan {
    display: flex;
    flex-direction: column;
    gap: 4px;
    min-width: 0;
  }
  .chead {
    display: flex;
    align-items: baseline;
    gap: 6px;
    margin-bottom: 2px;
  }
  .cname {
    font-size: 10px;
    font-weight: 700;
    color: var(--text-secondary);
  }
  .cerr {
    margin-left: auto;
    font-size: 8px;
    color: var(--text-dim);
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
  }

  .term {
    display: flex;
    align-items: center;
    gap: 7px;
  }
  .tl {
    width: 9px;
    font-size: 10px;
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
    align-items: center;
    gap: 4px;
    margin: -1px 0 3px 16px;
  }
  .chip {
    font-size: 8px;
    padding: 1px 4px;
    border-radius: 3px;
    background: var(--bg-base);
    color: var(--text-muted);
    box-shadow: var(--ring);
  }
  .chip-val {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 8.5px;
    font-weight: 600;
    color: var(--text-primary);
  }
  .chip-mul {
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
