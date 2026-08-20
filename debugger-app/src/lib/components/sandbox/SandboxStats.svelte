<script lang="ts">
  // PID stats view for the sandbox left column — the replay right-column
  // experience (PID readout with term bars + output graphs) fed by the
  // simulated per-tick data. Scoped to the movement under the playhead;
  // scrubbing a graph moves it.

  import GraphPanel from '../GraphPanel.svelte';
  import ExitGlyph from '../ExitGlyph.svelte';
  import { sandbox, getRoute, segmentAt } from '$lib/stores/sandbox.svelte';
  import { settings } from '$lib/stores/settings.svelte';
  import { prettyMovement, movementColor, isTurn, COLORS, PID_TERM } from '$lib/trace/labels';
  import { deriveMotion, type PidCfg, type TotalCfg } from '$lib/sandbox/sim';

  const route = $derived(getRoute());
  const seg = $derived(segmentAt(sandbox.time));

  // tick under the playhead (within the active segment)
  const tick = $derived.by(() => {
    if (!seg || !seg.ticks.length) return null;
    const ticks = seg.ticks;
    let lo = 0;
    let hi = ticks.length - 1;
    while (hi - lo > 1) {
      const mid = (lo + hi) >> 1;
      if (ticks[mid].t <= sandbox.time) lo = mid;
      else hi = mid;
    }
    return ticks[Math.min(lo, ticks.length - 1)];
  });

  const turnish = $derived(!!seg && isTurn(seg.fn));
  const openLoop = $derived(!!seg && (seg.fn === 'wait' || seg.fn === 'delay' || seg.fn === 'voldrive'));
  // trajectory has no drivePID/headingPID channel — its volts come
  // straight from the velocity controllers (config.cpp), so the P/I/D term
  // grid would just be showing synthesized numbers computed against the wrong
  // error signal. Treated like open loop for the readout, with its own honest
  // note instead of "no PID activity" (there IS closed-loop tracking, via the
  // RAMSETE law — just not in a P/I/D shape this card can show).
  const isTrajectory = $derived(!!seg && seg.fn === 'ramsete.trajectory');
  const turnChanLabel = $derived(turnish ? 'Turn PID' : 'Heading PID');

  // sandbox exit reasons → replay's ExitGlyph categories
  function glyphCategory(reason: string): string {
    if (reason === 'timeout' || reason === 'timed') return 'timeout';
    if (reason === 'perpLine') return 'perp';
    if (reason === 'dist' || reason === 'angle') return 'early';
    if (reason === 'schedule') return 'schedule';
    return 'settle';
  }

  // 'schedule' is the only sandbox exit reason that needs a friendlier label —
  // the rest (timeout, settle, perpLine, angle, dist) already read fine raw.
  function exitLabel(reason: string): string {
    return reason === 'schedule' ? 'schedule end' : reason;
  }

  const tStart = $derived(seg?.ticks.length ? seg.ticks[0].t : 0);
  const tEnd = $derived(seg?.ticks.length ? seg.ticks[seg.ticks.length - 1].t : 1);

  // trajectory's "Trajectory" readout: tracking error is corrected by
  // the RAMSETE law directly (see runTrajectory in sim.ts), not a
  // P/I/D channel object, so there's no P/I/D breakdown to show here — de/he
  // stay unset on a trajectory tick. dp/tp are the law's COMMANDED speed/turn
  // rate, scaled the same volts-equivalent way every other movement's output
  // graph is; un-scale them back to physical units with this route's own
  // derived motion limits for the readout.
  const trajProgress = $derived.by(() => {
    if (!isTrajectory || !tick) return null;
    const motion = deriveMotion(sandbox.knobs);
    const topLinear = Math.max(1, motion.topLinear);
    const topAngular = Math.max(1, motion.topAngular);
    const span = Math.max(1, tEnd - tStart);
    return {
      v: ((tick.dp ?? 0) / 12) * topLinear,
      w: ((tick.tp ?? 0) / 12) * topAngular,
      frac: Math.max(0, Math.min(1, (tick.t - tStart) / span))
    };
  });

  // --- channel cards (replay PidReadout shape, from the synthesized terms) ---
  type Chan = {
    label: string;
    unit: string;
    kp: number;
    ki: number;
    kd: number;
    e: number;
    ie: number;
    dei: number;
    termP: number;
    termI: number;
    termD: number;
    output: number;
  };

  const channels = $derived.by<Chan[]>(() => {
    if (!seg || !tick || openLoop || isTrajectory) return [];
    const out: Chan[] = [];
    const k = sandbox.knobs;
    if (!turnish && tick.de !== undefined) {
      const kp = k.driveKp;
      const ki = kp * 0.04;
      const kd = kp * 0.08;
      out.push({
        label: 'Drive PID',
        unit: 'in',
        kp,
        ki,
        kd,
        e: tick.de,
        ie: ki > 0 ? (tick.dI ?? 0) / ki : 0,
        dei: kd > 0 ? (tick.dD ?? 0) / kd : 0,
        termP: tick.dP ?? 0,
        termI: tick.dI ?? 0,
        termD: tick.dD ?? 0,
        output: tick.dp ?? 0
      });
    }
    if (tick.he !== undefined) {
      const kp = turnish ? k.turnKp : k.headingKp;
      const ki = kp * 0.04;
      const kd = kp * 0.01;
      out.push({
        label: turnChanLabel,
        unit: '°',
        kp,
        ki,
        kd,
        e: tick.he,
        ie: ki > 0 ? (tick.tI ?? 0) / ki : 0,
        dei: kd > 0 ? (tick.tD ?? 0) / kd : 0,
        termP: tick.tP ?? 0,
        termI: tick.tI ?? 0,
        termD: tick.tD ?? 0,
        output: tick.tp ?? 0
      });
    }
    return out;
  });
  const cols = $derived(Math.min(Math.max(channels.length, 1), 2));

  // --- graphs ---
  const driveNormal = $derived(
    seg ? [{ data: seg.ticks.map((k) => ({ t: k.t, v: k.dp ?? 0 })), color: COLORS.drive }] : []
  );
  const turnNormal = $derived(
    seg ? [{ data: seg.ticks.map((k) => ({ t: k.t, v: k.tp ?? 0 })), color: COLORS.turn }] : []
  );
  const driveBreakdown = $derived(
    seg
      ? [
          { data: seg.ticks.map((k) => ({ t: k.t, v: k.dP ?? 0 })), color: PID_TERM.p },
          { data: seg.ticks.map((k) => ({ t: k.t, v: k.dI ?? 0 })), color: PID_TERM.i },
          { data: seg.ticks.map((k) => ({ t: k.t, v: k.dD ?? 0 })), color: PID_TERM.d }
        ]
      : []
  );
  const turnBreakdown = $derived(
    seg
      ? [
          { data: seg.ticks.map((k) => ({ t: k.t, v: k.tP ?? 0 })), color: PID_TERM.p },
          { data: seg.ticks.map((k) => ({ t: k.t, v: k.tI ?? 0 })), color: PID_TERM.i },
          { data: seg.ticks.map((k) => ({ t: k.t, v: k.tD ?? 0 })), color: PID_TERM.d }
        ]
      : []
  );
  const driveSeries = $derived(settings.showBreakdown ? driveBreakdown : driveNormal);
  const turnSeries = $derived(settings.showBreakdown ? turnBreakdown : turnNormal);
  const pidLegend = [
    { label: 'P', color: PID_TERM.p },
    { label: 'I', color: PID_TERM.i },
    { label: 'D', color: PID_TERM.d }
  ];
  const driveActive = $derived(driveNormal[0]?.data.some((d) => Math.abs(d.v) > 0.1) ?? false);
  const turnActive = $derived(turnNormal[0]?.data.some((d) => Math.abs(d.v) > 0.1) ?? false);

  function scrub(t: number) {
    sandbox.playing = false;
    sandbox.time = Math.max(0, Math.min(route.sim.duration, t));
  }

  const fmt = (n: number, d = 2) => (Number.isFinite(n) ? n.toFixed(d) : '—');
  const signed = (n: number) => `${n >= 0 ? '+' : '−'}${fmt(Math.abs(n))}`;
  const errText = (c: Chan) => `${fmt(c.e, c.unit === '°' ? 1 : 2)}${c.unit === '°' ? '°' : ' in'}`;
  // fixed 0–12 V scale, same as replay
  const barPct = (v: number) => Math.min(Math.abs(v) / 12, 1) * 100;

  // config rows for the footer — only non-default/telling fields
  function cfgRows(c: PidCfg | TotalCfg | undefined): { k: string; v: string }[] {
    if (!c) return [];
    const rows: { k: string; v: string }[] = [];
    const push = (k: string, v: number, on = v !== 0) => on && rows.push({ k, v: fmt(v, v % 1 === 0 ? 0 : 2) });
    push('maxOutput', c.maxOutput, c.maxOutput !== 12);
    push('minOutput', c.minOutput);
    push('maxAccelRate', c.maxAccelRate);
    push('maxDecelRate', c.maxDecelRate);
    if ('targetOffset' in c) push('targetOffset', c.targetOffset);
    return rows;
  }
</script>

<div class="col">
  <section class="panel readout">
    <div class="head">
      <span class="lbl">PID readout</span>
      <span class="sub">at playhead</span>
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

    {#if seg && tick}
      <div class="rhead">
        <span class="dot" style="background: {movementColor(seg.fn)}"></span>
        <span class="mv">{prettyMovement(seg.fn)}</span>
        <span class="exit">
          <ExitGlyph category={glyphCategory(seg.exit)} size={13} />
          {exitLabel(seg.exit)}
        </span>
      </div>

      {#if openLoop}
        <div class="empty">Open loop — direct output, no PID activity during this call.</div>
      {:else if isTrajectory && trajProgress}
        <div class="empty">
          A trajectory has no drive/turn PID channels — the RAMSETE law corrects tracking error directly, not a P/I/D controller, so there's
          no breakdown to show here. At the playhead: commanded speed {fmt(trajProgress.v, 1)} in/s, commanded turn rate {fmt(trajProgress.w, 0)}°/s —
          {fmt(trajProgress.frac * 100, 0)}% through the schedule.
        </div>
      {:else}
        <div class="grid" style="grid-template-columns: repeat({cols}, minmax(0, 1fr));">
          {#each channels as c (c.label)}
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
                <!-- Δe is per-millisecond, so it lives around 0.0x — 2dp reads as 0.00 -->
                <span class="chip">Δe</span><span class="chip-val">{fmt(c.dei, 4)}</span>
              </div>

              <div class="outrow"><span class="olabel">Output</span><span class="oval">{fmt(c.output)} V</span></div>
            </div>
          {/each}
        </div>
      {/if}

      <!-- governor configs in effect for this movement -->
      <div class="cfgs">
        {#each [
          { label: 'drivePID', cfg: seg.pids?.drive },
          turnish ? { label: 'turnPID', cfg: seg.pids?.turn } : { label: 'headingPID', cfg: seg.pids?.heading },
          { label: 'totalOutput', cfg: seg.pids?.total }
        ] as ch (ch.label)}
          {@const rows = cfgRows(ch.cfg)}
          <div class="cfg">
            <span class="gname">{ch.label}</span>
            {#if rows.length}
              {#each rows as r (r.k)}
                <span class="crow"><code>{r.k}</code> {r.v}</span>
              {/each}
            {:else}
              <span class="crow quiet">defaults</span>
            {/if}
          </div>
        {/each}
      </div>
    {:else}
      <div class="empty">Scrub or play the route — the movement under the playhead shows its PID stats here.</div>
    {/if}
  </section>

  <GraphPanel
    title="Drive PID"
    grow={1}
    accent="#e8e8e8"
    active={driveActive}
    series={driveSeries}
    legend={settings.showBreakdown ? pidLegend : []}
    {tStart}
    {tEnd}
    playheadT={sandbox.time}
    onscrub={scrub}
  />
  <GraphPanel
    title={turnChanLabel}
    grow={1}
    accent="#e8e8e8"
    active={turnActive}
    series={turnSeries}
    legend={settings.showBreakdown ? pidLegend : []}
    {tStart}
    {tEnd}
    playheadT={sandbox.time}
    onscrub={scrub}
  />
</div>

<style>
  .col {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: var(--gap);
    min-height: 0;
    overflow-y: auto;
  }

  .readout {
    flex-shrink: 0;
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
  .sub {
    font-size: 8.5px;
    color: var(--text-ghost);
  }

  /* Breakdown switch — same control as replay's PidReadout */
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

  .rhead {
    display: flex;
    align-items: center;
    gap: 7px;
  }
  .dot {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    flex-shrink: 0;
  }
  .mv {
    font-size: 11px;
    font-weight: 700;
    color: var(--text-primary);
    flex: 1;
  }
  .exit {
    display: flex;
    align-items: center;
    gap: 4px;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 8.5px;
    color: var(--text-muted);
  }

  /* channel cards — replay PidReadout layout */
  .grid {
    display: grid;
    gap: 12px;
  }
  .chan {
    display: flex;
    flex-direction: column;
    gap: 5px;
    min-width: 0;
  }
  .chead {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 6px;
    margin-bottom: 1px;
  }
  .cname {
    font-size: 9.5px;
    font-weight: 700;
    color: var(--text-secondary);
  }
  .cerr {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 8.5px;
    color: var(--text-muted);
    font-variant-numeric: tabular-nums;
  }

  .term {
    display: flex;
    align-items: center;
    gap: 6px;
  }
  .tl {
    font-size: 9px;
    font-weight: 800;
    width: 8px;
    flex-shrink: 0;
  }
  .bar {
    flex: 1;
    height: 4px;
    border-radius: 2px;
    background: var(--bg-base);
    box-shadow: var(--ring);
    overflow: hidden;
    min-width: 0;
  }
  .fill {
    height: 100%;
    border-radius: 2px;
    transition: width 0.08s linear;
  }
  .tv {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 8.5px;
    font-weight: 600;
    color: var(--text-secondary);
    width: 46px;
    text-align: right;
    flex-shrink: 0;
    font-variant-numeric: tabular-nums;
  }

  .chips {
    display: flex;
    align-items: baseline;
    gap: 3px;
    padding-left: 14px;
    margin-top: -2px;
  }
  .chip {
    font-size: 7.5px;
    font-weight: 700;
    color: var(--text-ghost);
  }
  .chip-val {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 8px;
    color: var(--text-muted);
    font-variant-numeric: tabular-nums;
  }
  .chip-mul {
    font-size: 7.5px;
    color: var(--text-ghost);
  }

  .outrow {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    border-top: 1px solid rgba(255, 255, 255, 0.06);
    padding-top: 4px;
    margin-top: 2px;
  }
  .olabel {
    font-size: 8.5px;
    font-weight: 600;
    color: var(--text-muted);
  }
  .oval {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 10px;
    font-weight: 700;
    color: var(--text-primary);
    font-variant-numeric: tabular-nums;
  }

  .cfgs {
    display: flex;
    flex-direction: column;
    gap: 4px;
    border-top: 1px solid rgba(255, 255, 255, 0.06);
    padding-top: 8px;
  }
  .cfg {
    display: flex;
    align-items: baseline;
    gap: 8px;
    flex-wrap: wrap;
  }
  .gname {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9px;
    font-weight: 700;
    color: #569cd6;
    min-width: 74px;
  }
  .crow {
    font-size: 8.5px;
    color: var(--text-secondary);
    font-variant-numeric: tabular-nums;
  }
  .crow code {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 8.5px;
    color: #9cdcfe;
  }
  .crow.quiet {
    color: var(--text-ghost);
  }

  .empty {
    font-size: 9.5px;
    color: var(--text-ghost);
    line-height: 1.5;
  }
</style>
