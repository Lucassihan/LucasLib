<script lang="ts">
  import { traceState, movementAt, poseAt, tickAt } from '$lib/stores/trace.svelte';
  import { prettyMovement, movementColor, isTurn, isVolDrive, isTrajectory } from '$lib/trace/labels';
  import ExitGlyph from './ExitGlyph.svelte';
  import HeadingDial from './HeadingDial.svelte';
  import GovernorsCard from './GovernorsCard.svelte';
  import type { Movement } from '$lib/trace/types';

  const m = $derived(movementAt(traceState.time));
  const count = $derived(traceState.trace.movements.length);
  const pose = $derived(poseAt(traceState.time));
  const tk = $derived(tickAt(traceState.time));

  // Staged movements get a stage badge row (latched one-way). moveTo splits at
  // headingDisableDist (2 stages); boomerang runs the 3-stage ghost controller.
  const stageDef = $derived.by<{ t: string; sub: string }[] | null>(() => {
    if (!m) return null;
    if (m.type === 'boomerangTo') {
      return [
        { t: 'FAR', sub: 'chase ghost' },
        { t: 'MID', sub: 'chase carrot' },
        { t: 'END', sub: 'drive to end' }
      ];
    }
    if (m.type === 'moveTo') {
      return [
        { t: 'FAR', sub: 'point heading' },
        { t: 'CLOSE', sub: 'lock heading' }
      ];
    }
    if (m.type === 'ramsete.moveTo') {
      return [
        { t: 'FAR', sub: 'point + lyapunov' },
        { t: 'CLOSE', sub: 'lock heading' }
      ];
    }
    return null;
  });
  const curStage = $derived(tk ? tk.stage : 0);

  // Flash the badge when a stage latches under the playhead (matches the
  // field's pulse ring). Movement changes reset stage to 0 — no flash.
  let flashStage = $state(-1);
  let prevStage = -1;
  $effect(() => {
    const s = curStage;
    if (s > prevStage && prevStage >= 0) {
      flashStage = s;
      setTimeout(() => (flashStage = -1), 550);
    }
    prevStage = s;
  });

  // Boomerang slip-cap throttle, live at the cursor. Only meaningful when the
  // move sets a leadChasePowerLimit. maxSlip = √(grip·|r|); the cap binds when
  // the drive output sits at it (and the cap is below the motor ceiling).
  const throttle = $derived.by(() => {
    if (!m || m.type !== 'boomerangTo' || m.leadChasePowerLimit <= 0 || !tk) return null;
    const straight = tk.arcRadius > 1000;
    const drive = tk.drivePow;
    const maxSlip = straight ? null : tk.maxSlip;
    let clamping = false;
    if (maxSlip !== null && maxSlip > 0 && maxSlip <= 12.05 && Math.abs(drive) >= maxSlip - 0.05) {
      clamping = true;
    }
    let capPct = null; // cap marker position on the 0–12 V drive bar
    if (maxSlip !== null && maxSlip <= 12) capPct = (maxSlip / 12) * 100;
    return {
      grip: m.leadChasePowerLimit,
      arc: straight ? null : tk.arcRadius,
      maxSlip,
      drive,
      clamping,
      drivePct: Math.min(Math.abs(drive) / 12, 1) * 100,
      capPct
    };
  });

  // the actual error when the movement exited (its last tick)
  function exitError(mv: Movement): number {
    const ticks = traceState.trace.ticks;
    for (let i = ticks.length - 1; i >= 0; i--) {
      if (ticks[i].mi === mv.index) {
        return Math.abs(isTurn(mv.type) ? ticks[i].headingError : ticks[i].lateralError);
      }
    }
    return 0;
  }

  const POINT = new Set([
    'facePoint',
    'moveTo',
    'ramsete.moveTo',
    'boomerangTo',
    'swingOnLeftToPoint',
    'swingOnRightToPoint'
  ]);

  function paramRows(m: Movement): { k: string; v: string }[] {
    const r: { k: string; v: string }[] = [];
    const t = m.type;
    if (POINT.has(t)) r.push({ k: 'Target', v: `(${m.targetX}, ${m.targetY})` });
    if (t === 'face') r.push({ k: 'Heading', v: `${m.targetH}°` });
    if (t === 'drive' || t === 'driveForward' || t === 'driveBackward') {
      r.push({ k: 'Distance', v: `${m.distance} in` });
      r.push({ k: 'Heading', v: `${m.heading}°` });
    }
    if (t === 'boomerangTo') {
      r.push({ k: 'Heading', v: `${m.targetH}°` });
      r.push({ k: 'Lead', v: `${m.dLead}` });
      r.push({ k: 'Ghost lead', v: `${m.gLead}` });
      if (m.leadChasePowerLimit > 0) {
        r.push({ k: 'Chase limit', v: `${m.leadChasePowerLimit}` });
      }
    }
    if (t === 'ramsete.moveTo' && traceState.trace.header.kLat >= 0) {
      r.push({ k: 'kLat', v: `${traceState.trace.header.kLat}` });
    }
    if (t === 'voldrive') {
      r.push({ k: 'Left', v: `${m.leftVolts} V` });
      r.push({ k: 'Right', v: `${m.rightVolts} V` });
      r.push({ k: 'Duration', v: `${((m.endTime - m.startTime) / 1000).toFixed(2)} s` });
    }
    if (t.startsWith('swing')) r.push({ k: 'Partiality', v: `${m.partiality}` });
    if (m.direction !== 0) r.push({ k: 'Direction', v: m.direction > 0 ? 'CW' : 'CCW' });
    if (m.backwards) r.push({ k: 'Backwards', v: 'yes' });
    return r;
  }

  const unit = (m: Movement) => (isTurn(m.type) ? '°' : 'in');

  // exitDist carries the settle activation zone on BOTH channels — ec.settleWithin.
  // Only the unit differs: degrees on a turn, inches on a lateral.
  function settleZone(m: Movement): { value: number; u: string } {
    return { value: m.exitDist, u: unit(m) };
  }

  function triggeredLabel(reason: string): string {
    return { settle: 'Settle', timeout: 'Timeout', perpLine: 'Perp line', angle: 'Angle', dist: 'Distance', schedule: 'Schedule' }[reason] ?? reason;
  }

  // Traces recorded before the library reported its real ending label every
  // trajectory "timeout", because the timeout was the only armed condition there
  // was to guess from. A movement that ended well before its timeout could fire
  // demonstrably didn't exit on it, so read it as the schedule finishing.
  function effectiveExit(m: Movement): string {
    if (
      isTrajectory(m.type) &&
      m.exitReason === 'timeout' &&
      m.exitTimeout > 0 &&
      m.endTime - m.startTime < m.exitTimeout - 50
    ) {
      return 'schedule';
    }
    return m.exitReason;
  }

  /** Baked length of a trajectory's schedule, in seconds. 0 if the trace has no [SCHEDULE]. */
  function scheduleSeconds(m: Movement): number {
    const rows = traceState.trace.schedules[m.index];
    return rows?.length ? rows[rows.length - 1].t / 1000 : 0;
  }

  function triggeredDetail(m: Movement): string {
    switch (effectiveExit(m)) {
      case 'settle': {
        const z = settleZone(m);
        const zone = z.value > 0 ? `within ${z.value} ${z.u}` : 'in zone';
        // settleD carries ec.v now: a chassis SPEED, not a PID D term
        const sp = isTurn(m.type) ? `${m.settleD} rad/s` : `${m.settleD} in/s`;
        return m.settleD !== -1 ? `${zone}, slowing and under ${sp}` : `${zone} (no settle gate)`;
      }
      case 'timeout':
        return `${m.exitTimeout} ms elapsed`;
      case 'perpLine':
        return m.exitPerpLine > 0
          ? `crossed the ⊥ line, ${m.exitPerpLine} in short of the target`
          : 'crossed the ⊥ line through the target';
      case 'angle':
        return `within ${m.exitAng}° of target, no settle gate`;
      case 'dist':
        return `within ${m.exitPerpLine} in of the commanded distance`;
      // The trajectory's normal ending, and not an exit condition at all: the
      // reference clock ran past the schedule's last row. The gap between the
      // schedule's length and the movement's is how much the clock was braked.
      case 'schedule': {
        const plan = scheduleSeconds(m);
        const real = (m.endTime - m.startTime) / 1000;
        if (!plan) return 'reference clock reached the last row';
        return `reference clock reached the last row · ${plan.toFixed(2)} s of schedule in ${real.toFixed(2)} s`;
      }
      default:
        return '';
    }
  }

  function glyphCategory(reason: string): string {
    if (reason === 'timeout') return 'timeout';
    if (reason === 'perpLine') return 'perp';
    if (reason === 'angle' || reason === 'dist') return 'early';
    if (reason === 'schedule') return 'schedule';
    return 'settle';
  }

  function armed(m: Movement): { k: string; v: string }[] {
    const out: { k: string; v: string }[] = [];
    if (m.exitTimeout !== -1 && effectiveExit(m) !== 'timeout') {
      out.push({ k: 'Timeout', v: `${m.exitTimeout} ms` });
    }
    if (effectiveExit(m) !== 'settle') {
      const z = settleZone(m);
      const sp = isTurn(m.type) ? `${m.settleD} rad/s` : `${m.settleD} in/s`;
      if (z.value > 0 && m.settleD !== -1) out.push({ k: 'Settle', v: `${z.value} ${z.u} · slowing, under ${sp}` });
    }
    // d = 0 is armed and meaningful (line on the target); only -1 is off
    if (!isTurn(m.type) && m.exitPerpLine !== -1 && effectiveExit(m) !== 'perpLine') {
      out.push({ k: 'Perp line', v: m.exitPerpLine > 0 ? `${m.exitPerpLine} in early` : 'on target' });
    }
    if (isTurn(m.type) && m.exitAng !== -1 && effectiveExit(m) !== 'angle') {
      out.push({ k: 'Angle', v: `${m.exitAng}° · no settle gate` });
    }
    return out;
  }

  const durationS = $derived(m ? ((m.endTime - m.startTime) / 1000).toFixed(2) : '0');
</script>

<section class="panel inspector">
  {#if m}
    <div class="card">
      <div class="name-row">
        <span class="dot" style="background: {movementColor(m.type)}"></span>
        <span class="name">{prettyMovement(m.type)}</span>
        <span class="idx">{m.index + 1} / {count}</span>
      </div>
      <div class="sub">{m.type} · {durationS} s</div>

      <div class="params">
        <div class="group-label">Parameters</div>
        {#each paramRows(m) as r}
          <div class="row"><span class="k">{r.k}</span><span class="v">{r.v}</span></div>
        {/each}
      </div>

      {#if stageDef}
        <div class="stage-block">
          <div class="group-label">Stage</div>
          <div class="stage-badges">
            {#each stageDef as s, i}
              <div class="stage-badge" class:on={i === curStage} class:flash={i === flashStage}>
                <div class="sb-t">{s.t}</div>
                <div class="sb-sub">{s.sub}</div>
              </div>
            {/each}
          </div>
        </div>
      {/if}

      {#if throttle}
        <div class="throttle-block">
          <div class="group-label">Throttle · slip cap</div>

          <!-- inputs: a tuning constant + a live measurement -->
          <div class="thr-in">
            <div class="thr-cell">
              <span class="thr-tag const">const</span>
              <span class="thr-name">grip</span>
              <span class="thr-val">{throttle.grip}<span class="thr-u"> V²/in</span></span>
            </div>
            <div class="thr-cell">
              <span class="thr-tag live">measured</span>
              <span class="thr-name">arc radius r</span>
              <span class="thr-val">{throttle.arc === null ? '∞' : throttle.arc.toFixed(1)}<span class="thr-u"> in</span></span>
            </div>
          </div>

          <!-- computed cap -->
          <div class="thr-calc">
            <span class="thr-eq">max slip = √(grip · |r|)</span>
            <span class="thr-cap">{throttle.maxSlip === null ? '—' : `${throttle.maxSlip.toFixed(1)} V`}</span>
          </div>

          <!-- effect on the drive output -->
          <div class="thr-drive">
            <span class="thr-dlabel">drive</span>
            <div class="thr-bar">
              <div class="thr-fill" class:clamp={throttle.clamping} style="width: {throttle.drivePct}%"></div>
              {#if throttle.capPct !== null}
                <div class="thr-capmark" style="left: {throttle.capPct}%"></div>
              {/if}
            </div>
            <span class="thr-dval" class:clamp={throttle.clamping}>{throttle.drive.toFixed(1)} V</span>
          </div>
          {#if throttle.clamping}
            <div class="thr-note">drive held at the slip cap</div>
          {:else}
            <div class="thr-note">drive below cap — no slip limiting</div>
          {/if}
        </div>
      {/if}
    </div>

    {#if isVolDrive(m.type)}
      <div class="card">
        <div class="group-label">Termination</div>
        <div class="exit-reason">
          <span>Timed</span>
        </div>
        <div class="exit-detail">Ran for {((m.endTime - m.startTime) / 1000).toFixed(2)} s.</div>
      </div>
    {:else}
    <div class="card">
      <div class="group-label">Exit condition</div>
      <div class="exit-reason">
        <ExitGlyph category={glyphCategory(effectiveExit(m))} size={17} />
        <span>{triggeredLabel(effectiveExit(m))}</span>
      </div>
      <div class="exit-detail">{triggeredDetail(m)}</div>

      {#if effectiveExit(m) === 'settle'}
        <div class="row exit-stat">
          <span class="k">Exit error</span>
          <span class="v">{exitError(m).toFixed(isTurn(m.type) ? 1 : 2)} {unit(m)}</span>
        </div>
      {/if}

      <div class="armed">
        <div class="armed-label">Also armed · not triggered</div>
        {#each armed(m) as a}
          <div class="row"><span class="k">{a.k}</span><span class="v">{a.v}</span></div>
        {/each}
      </div>
    </div>
    {/if}

    {#if !isVolDrive(m.type)}
      <GovernorsCard />
    {/if}

    <div class="card">
      <div class="group-label">Robot Pose</div>
      <div class="dial-wrap"><HeadingDial heading={pose.h} size={104} /></div>
      <div class="hdg-hero">
        <div class="hdg-num">{pose.h.toFixed(1)}°</div>
        <div class="hdg-cap">HEADING</div>
      </div>
      <div class="xy">
        <div class="xy-tile">
          <span class="xy-k">X</span>
          <span class="xy-v">{pose.x.toFixed(2)}<span class="xy-u"> in</span></span>
        </div>
        <div class="xy-tile">
          <span class="xy-k">Y</span>
          <span class="xy-v">{pose.y.toFixed(2)}<span class="xy-u"> in</span></span>
        </div>
      </div>
    </div>
  {/if}
</section>

<style>
  .inspector {
    flex: 0 0 250px;
    min-height: 0;
    overflow-y: auto;
    padding: 10px;
    display: flex;
    flex-direction: column;
    gap: 9px;
  }

  /* each section is its own distinct card, raised off the grey box */
  .card {
    background: var(--bg-elevated);
    border-radius: 7px;
    padding: 11px 12px;
    box-shadow: var(--ring);
  }

  .name-row {
    display: flex;
    align-items: center;
    gap: 7px;
  }
  .dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    flex-shrink: 0;
  }
  .name {
    font-size: 15px;
    font-weight: 700;
    color: var(--text-emphasis);
  }
  .idx {
    margin-left: auto;
    font-size: 10px;
    font-weight: 400;
    color: var(--text-dim);
  }
  .sub {
    margin-top: 4px;
    font-size: 9px;
    font-weight: 400;
    color: var(--text-ghost);
  }

  /* parameters share the header card, divided from the title above */
  .params {
    margin-top: 11px;
    padding-top: 10px;
    border-top: 1px solid var(--border-subtle);
  }
  /* stage badges live in the movement card, divided from the params above */
  .stage-block {
    margin-top: 11px;
    padding-top: 10px;
    border-top: 1px solid var(--border-subtle);
  }
  /* boomerang slip-cap throttle, at the bottom of the movement card */
  .throttle-block {
    margin-top: 11px;
    padding-top: 10px;
    border-top: 1px solid var(--border-subtle);
  }
  /* tier 1 — inputs (const + measured), tagged so the source is obvious */
  .thr-in {
    display: flex;
    gap: 7px;
    margin-bottom: 7px;
  }
  .thr-cell {
    flex: 1;
    min-width: 0;
    background: var(--bg-base);
    border-radius: var(--radius-small);
    padding: 6px 8px;
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.03);
  }
  .thr-tag {
    display: inline-block;
    font-size: 7px;
    font-weight: 700;
    letter-spacing: 0.04em;
    text-transform: uppercase;
    padding: 1px 4px;
    border-radius: 3px;
  }
  .thr-tag.const {
    background: rgba(255, 255, 255, 0.08);
    color: var(--text-muted);
  }
  .thr-tag.live {
    background: var(--accent-bg);
    color: var(--accent-text);
  }
  .thr-name {
    display: block;
    font-size: 9px;
    color: var(--text-dim);
    margin-top: 4px;
  }
  .thr-val {
    display: block;
    font-size: 13px;
    font-weight: 600;
    color: var(--text-primary);
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
  }
  .thr-u {
    font-size: 8px;
    font-weight: 400;
    color: var(--text-dim);
  }
  /* tier 2 — computed cap */
  .thr-calc {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    gap: 6px;
    padding: 6px 8px;
    border-radius: var(--radius-small);
    background: var(--bg-base);
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.03);
    margin-bottom: 8px;
  }
  .thr-eq {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9.5px;
    color: var(--text-muted);
  }
  .thr-cap {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 13px;
    font-weight: 700;
    color: var(--text-emphasis);
  }
  /* tier 3 — effect on drive */
  .thr-drive {
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .thr-dlabel {
    width: 30px;
    font-size: 9px;
    color: var(--text-dim);
  }
  .thr-bar {
    position: relative;
    flex: 1;
    min-width: 0;
    height: 8px;
    border-radius: 4px;
    background: var(--bg-base);
    overflow: hidden;
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.03);
  }
  .thr-fill {
    position: absolute;
    left: 0;
    top: 0;
    height: 100%;
    border-radius: 4px;
    background: #cfcfcf;
  }
  .thr-fill.clamp {
    background: #f87171;
  }
  .thr-capmark {
    position: absolute;
    top: -1px;
    bottom: -1px;
    width: 2px;
    margin-left: -1px;
    background: #f87171;
    z-index: 1;
  }
  .thr-dval {
    width: 50px;
    text-align: right;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 10px;
    font-weight: 600;
    color: var(--text-primary);
  }
  .thr-dval.clamp {
    color: #f87171;
  }
  .thr-note {
    font-size: 8px;
    color: var(--text-ghost);
    margin-top: 5px;
  }

  .group-label {
    margin-bottom: 9px;
    font-size: 10px;
    font-weight: 700;
    color: var(--text-secondary);
    letter-spacing: 0.02em;
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
    text-align: right;
  }

  .exit-reason {
    display: flex;
    align-items: center;
    gap: 7px;
    font-size: 15px;
    font-weight: 700;
    color: var(--text-emphasis);
    margin-top: 1px;
  }
  .exit-detail {
    font-size: 9px;
    font-weight: 400;
    color: var(--text-secondary);
    margin-top: 1px;
  }
  .exit-stat {
    margin-top: 8px;
  }

  .armed {
    margin-top: 11px;
    padding: 9px 10px;
    border-radius: var(--radius-small);
    background: var(--bg-base);
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.03);
  }
  .armed-label {
    font-size: 8px;
    font-weight: 600;
    color: var(--text-muted);
    margin-bottom: 6px;
  }
  .armed .k,
  .armed .v {
    color: var(--text-dim);
    font-weight: 400;
  }

  /* Robot Pose card — heading-hero layout (compact) */
  .dial-wrap {
    display: flex;
    justify-content: center;
    margin-top: 1px;
  }
  .hdg-hero {
    text-align: center;
    margin: 0 0 8px;
  }
  .hdg-num {
    font-size: 17px;
    font-weight: 700;
    color: var(--text-emphasis);
    line-height: 1;
  }
  .hdg-cap {
    font-size: 7px;
    font-weight: 600;
    color: var(--text-muted);
    letter-spacing: 0.1em;
    margin-top: 2px;
  }
  .xy {
    display: flex;
    gap: 8px;
  }
  .xy-tile {
    flex: 1;
    background: var(--bg-base);
    border-radius: var(--radius-small);
    padding: 7px 9px;
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.03);
  }
  .xy-k {
    display: block;
    font-size: 9px;
    font-weight: 400;
    color: var(--text-dim);
  }
  .xy-v {
    display: block;
    font-size: 13px;
    font-weight: 600;
    color: var(--text-primary);
    margin-top: 1px;
  }
  .xy-u {
    font-size: 9px;
    font-weight: 400;
    color: var(--text-dim);
  }

  /* Stage badges */
  .stage-badges {
    display: flex;
    gap: 6px;
  }
  .stage-badge {
    flex: 1;
    text-align: center;
    border-radius: 6px;
    padding: 7px 4px;
    background: var(--bg-base);
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.04);
  }
  .sb-t {
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 0.05em;
    color: var(--text-dim);
  }
  .sb-sub {
    font-size: 8px;
    color: var(--text-ghost);
    margin-top: 2px;
  }
  .stage-badge.on {
    background: var(--accent-bg);
    box-shadow: inset 0 0 0 1px var(--accent);
  }
  .stage-badge.on .sb-t {
    color: var(--accent-text);
  }
  .stage-badge.flash {
    animation: badge-flash 0.5s ease;
  }
  @keyframes badge-flash {
    0% {
      transform: scale(1);
    }
    30% {
      transform: scale(1.1);
      box-shadow:
        inset 0 0 0 1px var(--accent),
        0 0 16px color-mix(in srgb, var(--accent) 45%, transparent);
    }
    100% {
      transform: scale(1);
    }
  }
</style>
