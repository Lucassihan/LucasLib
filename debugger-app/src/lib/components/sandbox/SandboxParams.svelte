<script lang="ts">
  import { sandbox, getRoute } from '$lib/stores/sandbox.svelte';
  import { deriveMotion } from '$lib/sandbox/sim';
  import { trajectoryDuration } from '$lib/sandbox/ramsete';
  import { prettyMovement } from '$lib/trace/labels';
  import { GAMES, MODES } from '$lib/field/backdrops';
  import SliderRow from './SliderRow.svelte';

  const route = $derived(getRoute());

  // motion limits derived from the drivetrain spec (shown as a readout)
  const motion = $derived(deriveMotion(sandbox.knobs));
  const selSeg = $derived(
    sandbox.selected !== null ? route.sim.segments.find((s) => s.stmtIndex === sandbox.selected) ?? null : null
  );

  // trajectory's baked schedule, summarized for the Selected card —
  // arc length is the sum of consecutive baked rows' distance, which is an
  // honest measure since the rows themselves are arc-spaced samples of the
  // spline (see createTrajectory / arcSpacedParams in ramsete.ts).
  const trajStats = $derived.by(() => {
    const traj = selSeg?.traj;
    if (!traj) return null;
    const rows = traj.rows;
    let arc = 0;
    let topV = 0;
    for (let i = 0; i < rows.length; i++) {
      if (i > 0) arc += Math.hypot(rows[i].x - rows[i - 1].x, rows[i].y - rows[i - 1].y);
      topV = Math.max(topV, Math.abs(rows[i].v));
    }
    return { rows: rows.length, duration: trajectoryDuration(rows), arc, topV };
  });

  // per-movement heading-correction effort for the selected moveTo / driveForward / driveBackward
  const effortKind = $derived.by(() => {
    if (!selSeg) return null;
    if (selSeg.fn === 'moveTo') return 'moveTo' as const;
    if (selSeg.fn === 'driveForward' || selSeg.fn === 'driveBackward') return 'drive' as const;
    return null;
  });
  const effortDefault = $derived(
    effortKind === 'moveTo' ? sandbox.knobs.moveToHeadingEffort : sandbox.knobs.driveHeadingEffort
  );
  const selEffort = $derived(
    selSeg && sandbox.efforts[selSeg.stmtIndex] !== undefined ? sandbox.efforts[selSeg.stmtIndex] : effortDefault
  );
  function setSelEffort(v: number) {
    if (!selSeg) return;
    sandbox.efforts = { ...sandbox.efforts, [selSeg.stmtIndex]: v };
  }
  function clearSelEffort() {
    if (!selSeg) return;
    const next = { ...sandbox.efforts };
    delete next[selSeg.stmtIndex];
    sandbox.efforts = next;
  }

  // trim trailing zeros only AFTER a decimal point (never strip an integer's
  // own zeros, e.g. 300 must not become 3)
  const fmt = (v: number, d = 1) => {
    let s = v.toFixed(d);
    if (s.includes('.')) s = s.replace(/0+$/, '').replace(/\.$/, '');
    return s || '0';
  };

  // Drivetrain spec tiles: typed values; diameter gets quick-pick chips for the
  // standard VEX wheel sizes (rpm is free-form — any gearing is possible)
  const SPEC = [
    { key: 'wheelRpm' as const, label: 'Wheel rpm', unit: 'rpm', presets: [] as number[], min: 50, decimals: 0 },
    { key: 'wheelDiameter' as const, label: 'Wheel diameter', unit: 'in', presets: [2, 2.75, 3.25, 4], min: 1.5, decimals: 3 }
  ];
  function commitSpec(key: 'wheelRpm' | 'wheelDiameter', raw: string, min: number) {
    const n = parseFloat(raw);
    if (!Number.isNaN(n)) sandbox.knobs[key] = Math.max(min, n);
  }
</script>

<div class="col">
  <section class="panel group">
    <div class="gtitle">Field <span class="gsub">saved with this auton</span></div>
    <div class="fieldrows">
      <button class="frow solo" class:on={sandbox.field === 'perimeter'} onclick={() => (sandbox.field = 'perimeter')}>Empty field</button>
      {#each GAMES as g (g.key)}
        <div class="glabel">{g.label}</div>
        {#each MODES as mode (mode)}
          <div class="frow">
            <span class="fname">{mode === 'match' ? 'Match' : 'Skills'}</span>
            <div class="fvars">
              <button class="fvar" class:on={sandbox.field === `${g.key}-${mode}-h`} onclick={() => (sandbox.field = `${g.key}-${mode}-h`)}>
                Highlighted
              </button>
              <button class="fvar" class:on={sandbox.field === `${g.key}-${mode}`} onclick={() => (sandbox.field = `${g.key}-${mode}`)}>
                Plain
              </button>
            </div>
          </div>
        {/each}
      {/each}
    </div>
    <div class="note">
      Each auton keeps its own backdrop and saves it into the route file. Settings' field picker still drives Replay and the Trajectory
      Generator; it doesn't apply here.
    </div>
  </section>

  <section class="panel group">
    <div class="gtitle">Drivetrain <span class="gsub">6 × 11 W V5 motors — motion is derived</span></div>

    <div class="specgrid">
      {#each SPEC as sp (sp.key)}
        <div class="spec">
          <span class="slbl">{sp.label}</span>
          <div class="sval">
            <input
              class="sin"
              type="text"
              inputmode="decimal"
              value={fmt(sandbox.knobs[sp.key], sp.decimals)}
              onfocus={(e) => (e.currentTarget as HTMLInputElement).select()}
              onchange={(e) => commitSpec(sp.key, (e.currentTarget as HTMLInputElement).value, sp.min)}
              onkeydown={(e) => {
                if (e.key === 'Enter') (e.currentTarget as HTMLInputElement).blur();
              }}
            />
            <span class="sunit">{sp.unit}</span>
          </div>
          {#if sp.presets.length}
            <div class="chips">
              {#each sp.presets as p (p)}
                <button class="chip" class:on={sandbox.knobs[sp.key] === p} onclick={() => (sandbox.knobs[sp.key] = p)}>{p}</button>
              {/each}
            </div>
          {/if}
        </div>
      {/each}
    </div>
    <SliderRow label="Weight" min={1} max={20} step={0.25} unit="kg" value={sandbox.knobs.robotWeight} onchange={(v) => (sandbox.knobs.robotWeight = Math.max(1, v))} />
    <div class="note derived">
      → top <b>{fmt(motion.topLinear, 0)} in/s</b> · <b>{fmt(motion.topAngular, 0)}°/s</b> · accel
      <b>{fmt(motion.linAccel, 0)} in/s²</b> · <b>{fmt(motion.angAccel, 0)}°/s²</b>
    </div>
    <div class="note">
      Base weight 1 kg; angular numbers use the trackwidth from robot settings (width − 3 in = {fmt(sandbox.knobs.trackwidth)}).
    </div>
  </section>

  <section class="panel group">
    <div class="gtitle">PID gains <span class="gsub">movement aggressiveness</span></div>

    <SliderRow label="drive kP" min={0} max={10} step={0.005} decimals={3} unit="V/in" value={sandbox.knobs.driveKp} onchange={(v) => (sandbox.knobs.driveKp = v)} />
    <SliderRow label="turn kP" min={0} max={2} step={0.005} decimals={3} unit="V/°" value={sandbox.knobs.turnKp} onchange={(v) => (sandbox.knobs.turnKp = v)} />
    <SliderRow label="swing kP" min={0} max={2} step={0.005} decimals={3} unit="V/°" value={sandbox.knobs.swingKp} onchange={(v) => (sandbox.knobs.swingKp = v)} />
    <SliderRow label="heading kP" min={0} max={2} step={0.005} decimals={3} unit="V/°" value={sandbox.knobs.headingKp} onchange={(v) => (sandbox.knobs.headingKp = v)} />
    <div class="note">
      How hard each channel pushes on its error — turns and swings never overshoot regardless, since kP only shapes the approach under the braking limit.
    </div>
  </section>

  <section class="panel group">
    <div class="gtitle">Motion chaining <span class="gsub">momentum across early exits</span></div>

    <div class="row">
      <span class="rlbl">Wheels</span>
      <div class="seg">
        <button class:on={!sandbox.knobs.omniWheels} onclick={() => (sandbox.knobs.omniWheels = false)}>Grip</button>
        <button class:on={sandbox.knobs.omniWheels} onclick={() => (sandbox.knobs.omniWheels = true)}>Omni</button>
      </div>
    </div>
    <SliderRow label="Lateral momentum" min={0} max={1} step={0.01} decimals={2} capMax value={sandbox.knobs.lateralMomentum} onchange={(v) => (sandbox.knobs.lateralMomentum = v)} />
    <SliderRow label="Angular momentum" min={0} max={1} step={0.01} decimals={2} capMax value={sandbox.knobs.angularMomentum} onchange={(v) => (sandbox.knobs.angularMomentum = v)} />
    <div class="note">
      How much leftover speed or spin carries into the next movement on an early exit — omni wheels carry it much farther than grip wheels.
    </div>
  </section>

  <section class="panel group">
    <div class="gtitle">Heading correction effort <span class="gsub">sim-only behavior</span></div>

    {#if selSeg && effortKind}
      <SliderRow
        label={`${prettyMovement(selSeg.fn)} #${selSeg.stmtIndex}`}
        min={0}
        max={2}
        step={0.01}
        decimals={2}
        value={selEffort}
        onchange={setSelEffort}
      />
      <div class="note">
        This tunes only the selected movement; click another to tune it instead.
        {#if sandbox.efforts[selSeg.stmtIndex] !== undefined}
          <button class="mini" onclick={clearSelEffort}>reset to default ({fmt(effortDefault, 2)})</button>
        {/if}
      </div>
    {:else}
      <div class="note">Click a moveTo / driveForward / driveBackward on the field to tune how fast it corrects heading; untuned movements use the defaults below.</div>
    {/if}

    <SliderRow label="moveTo default" min={0} max={2} step={0.01} decimals={2} value={sandbox.knobs.moveToHeadingEffort} onchange={(v) => (sandbox.knobs.moveToHeadingEffort = v)} />
    <SliderRow label="driveForward()/driveBackward() default" min={0} max={2} step={0.01} decimals={2} value={sandbox.knobs.driveHeadingEffort} onchange={(v) => (sandbox.knobs.driveHeadingEffort = v)} />
  </section>

  <section class="panel group">
    <div class="gtitle">Library globals <span class="gsub">real tunables, set outside calls</span></div>

    <SliderRow label="headingDisableDist" min={0} max={12} step={0.25} unit="in" value={sandbox.knobs.headingDisableDist} onchange={(v) => (sandbox.knobs.headingDisableDist = v)} />
  </section>

  <section class="panel group">
    <div class="gtitle">RAMSETE <span class="gsub">trajectory — the law that tracks the baked schedule</span></div>

    <div class="subhdr">Chassis <span class="mono2">config.cpp — setRamseteSettings()</span></div>
    <SliderRow label="max velocity" min={5} max={150} step={1} unit="in/s" value={sandbox.knobs.rsMaxVel} onchange={(v) => (sandbox.knobs.rsMaxVel = v)} />
    <SliderRow label="max acceleration" min={10} max={300} step={5} unit="in/s²" value={sandbox.knobs.rsMaxAccel} onchange={(v) => (sandbox.knobs.rsMaxAccel = v)} />
    <div class="kv"><span>Trackwidth</span><span class="mono">{fmt(sandbox.knobs.trackwidth)} in <span class="mono2">— derived, not a knob</span></span></div>

    <div class="subhdr">Profile bake <span class="mono2">PathSettings — util/profile.h</span></div>
    <SliderRow label="velMargin" min={0.1} max={1} step={0.01} decimals={2} value={sandbox.knobs.rsVelMargin} onchange={(v) => (sandbox.knobs.rsVelMargin = v)} />
    <SliderRow label="dt" min={2} max={40} step={1} unit="ms" value={sandbox.knobs.rsDt} onchange={(v) => (sandbox.knobs.rsDt = v)} />
    <SliderRow label="endSpeed" min={0} max={90} step={1} unit="in/s" value={sandbox.knobs.rsEndSpeed} onchange={(v) => (sandbox.knobs.rsEndSpeed = v)} />
    <SliderRow label="finalDistance" min={0} max={24} step={0.5} unit="in" value={sandbox.knobs.rsFinalDistance} onchange={(v) => (sandbox.knobs.rsFinalDistance = v)} />
    <div class="note">
      The <span class="mono2">PathSettings</span> a route would author, tuned here instead. Per-segment ceilings live on the
      segment as <span class="mono2">.maxVel</span> / <span class="mono2">.maxAccel</span>.
    </div>

    <div class="note">
      trajectory runs the real RAMSETE law against a perfect actuator, so <span class="mono2">b</span>, <span class="mono2">ζ</span>,
      <span class="mono2">vMinGain</span> and <span class="mono2">clockCoupling</span> come from the library's own measured constants rather than a
      knob here.
    </div>

    <div class="subhdr">Point movement <span class="mono2">ramsete.moveTo()</span></div>
    <SliderRow label="kLat" min={0} max={0.2} step={0.002} decimals={3} unit="1/in" value={sandbox.knobs.rsKLat} onchange={(v) => (sandbox.knobs.rsKLat = v)} />
    <SliderRow label="headingDisableDist" min={0} max={12} step={0.25} decimals={2} unit="in" value={sandbox.knobs.rsHeadingDisableDist} onchange={(v) => (sandbox.knobs.rsHeadingDisableDist = v)} />
    <div class="note">Its own boundary — <span class="mono2">moveTo.cpp</span> hardcodes 0.5", where the classic moveTo and trajectory's chase both hand over at the shared <span class="mono2">turnDisableLength</span> above.</div>
    <div class="note">
      <span class="mono2">ramsete.moveTo</span> is the one thing in the module that does not use the velocity layer — it runs on
      <span class="mono2">drivePID</span> and <span class="mono2">headingPID</span> in volts, so <b>kLat is not comparable to the law's b</b>: b
      multiplies a speed in in/s, this multiplies a voltage. Raise it and the robot commits to the straight line to the target sooner; drop it to
      zero and the movement is a plain point-chase with no cross-track term at all.
    </div>
  </section>

  <section class="panel group">
    <div class="gtitle">Default exit conditions <span class="gsub">for ec fields you leave out</span></div>

    <div class="subhdr">Lateral <span class="mono2">LateralExitConditions</span></div>
    <SliderRow label="t" slider={false} min={-1} max={60000} step={50} decimals={0} unit="ms" value={sandbox.knobs.ecLatT} onchange={(v) => (sandbox.knobs.ecLatT = v)} />
    <SliderRow label="v" slider={false} min={-1} max={80} step={0.1} decimals={2} unit="in/s" value={sandbox.knobs.ecLatV} onchange={(v) => (sandbox.knobs.ecLatV = v)} />
    <SliderRow label="d" slider={false} min={-1} max={144} step={0.1} decimals={2} unit="in" value={sandbox.knobs.ecLatD} onchange={(v) => (sandbox.knobs.ecLatD = v)} />
    <SliderRow label="settleWithin" slider={false} min={0} max={144} step={0.1} decimals={2} unit="in" value={sandbox.knobs.ecLatWithin} onchange={(v) => (sandbox.knobs.ecLatWithin = v)} />

    <div class="subhdr">Turn <span class="mono2">TurnExitConditions</span></div>
    <SliderRow label="t" slider={false} min={-1} max={60000} step={50} decimals={0} unit="ms" value={sandbox.knobs.ecTurnT} onchange={(v) => (sandbox.knobs.ecTurnT = v)} />
    <SliderRow label="v" slider={false} min={-1} max={20} step={0.05} decimals={2} unit="rad/s" value={sandbox.knobs.ecTurnV} onchange={(v) => (sandbox.knobs.ecTurnV = v)} />
    <SliderRow label="d" slider={false} min={-1} max={180} step={0.25} decimals={2} unit="°" value={sandbox.knobs.ecTurnD} onchange={(v) => (sandbox.knobs.ecTurnD = v)} />
    <SliderRow label="settleWithin" slider={false} min={0} max={180} step={0.25} decimals={2} unit="°" value={sandbox.knobs.ecTurnWithin} onchange={(v) => (sandbox.knobs.ecTurnWithin = v)} />
    <div class="note">
      <b>-1 turns a field off</b> on both channels; turn <span class="mono2">v</span> is <b>rad/s</b> while
      <span class="mono2">settleWithin</span> is degrees, matching the library's own mismatch (checkTurnExit compares
      <span class="mono2">chassis.ω</span>, which updateVelocity computes in radians).
    </div>
  </section>

  {#if selSeg}
    <section class="panel group sel">
      <div class="gtitle">Selected — {prettyMovement(selSeg.fn)}</div>
      <div class="kv"><span>Duration</span><span class="mono">{(selSeg.ticks.length / 100).toFixed(2)} s</span></div>
      <div class="kv"><span>Start</span><span class="mono">({fmt(selSeg.start.x)}, {fmt(selSeg.start.y)}) · {fmt(selSeg.start.h, 0)}°</span></div>
      <div class="kv"><span>End</span><span class="mono">({fmt(selSeg.end.x)}, {fmt(selSeg.end.y)}) · {fmt(selSeg.end.h, 0)}°</span></div>
      <div class="kv"><span>Exit</span><span class="mono">{selSeg.exit}</span></div>
      <div class="kv"><span>Direction</span><span class="mono">{selSeg.backward ? 'backward' : 'forward'}</span></div>
      {#if trajStats}
        <div class="kv"><span>Baked rows</span><span class="mono">{trajStats.rows}</span></div>
        <div class="kv"><span>Schedule</span><span class="mono">{trajStats.duration.toFixed(2)} s</span></div>
        <div class="kv"><span>Path length</span><span class="mono">{fmt(trajStats.arc)} in</span></div>
        <div class="kv"><span>Top planned speed</span><span class="mono">{fmt(trajStats.topV)} in/s</span></div>
      {/if}
    </section>
  {/if}

  {#if route.sim.endPose}
    <section class="panel group total">
      <div class="kv"><span>Route time</span><span class="mono big">{(route.sim.duration / 1000).toFixed(2)} s</span></div>
      <div class="kv"><span>Ends at</span><span class="mono">({fmt(route.sim.endPose.x)}, {fmt(route.sim.endPose.y)}) · {fmt(route.sim.endPose.h, 0)}°</span></div>
    </section>
  {/if}
</div>

<style>
  .col {
    display: flex;
    flex-direction: column;
    gap: var(--gap);
    min-height: 0;
    overflow-y: auto;
  }
  .group {
    padding: 12px 14px;
    display: flex;
    flex-direction: column;
    gap: 8px;
    flex-shrink: 0;
  }
  .gtitle {
    font-size: 11px;
    font-weight: 700;
    color: var(--text-primary);
    display: flex;
    align-items: baseline;
    gap: 6px;
    flex-wrap: wrap;
  }
  .gsub {
    font-size: 8.5px;
    font-weight: 400;
    color: var(--text-ghost);
  }
  .row {
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .rlbl {
    font-size: 9.5px;
    color: var(--text-secondary);
    width: 108px;
    flex-shrink: 0;
  }
  .mini {
    background: none;
    border: none;
    color: var(--accent-text-dim);
    font-family: inherit;
    font-size: 8.5px;
    cursor: pointer;
    padding: 0;
    text-decoration: underline;
  }
  .mini:hover {
    color: var(--accent-text);
  }
  .note {
    font-size: 8.5px;
    color: var(--text-ghost);
    line-height: 1.45;
    margin: -2px 0 2px;
  }
  /* the derived-motion readout — slightly brighter than a plain note */
  .note.derived {
    color: var(--text-secondary);
    font-variant-numeric: tabular-nums;
  }
  .note.derived b {
    color: var(--text-primary);
    font-weight: 600;
  }

  /* drivetrain spec tiles: typed value + quick-pick chips, side by side */
  .specgrid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 8px;
  }
  .spec {
    display: flex;
    flex-direction: column;
    gap: 6px;
    background: var(--bg-base);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 8px 10px 9px;
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
    display: flex;
    align-items: baseline;
    gap: 4px;
  }
  .sin {
    width: 100%;
    min-width: 0;
    background: none;
    border: none;
    outline: none;
    padding: 0;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 17px;
    font-weight: 700;
    color: var(--text-primary);
    font-variant-numeric: tabular-nums;
    border-bottom: 1px solid rgba(255, 255, 255, 0.08);
    transition: border-color 0.15s ease;
  }
  .sin:hover {
    border-bottom-color: rgba(255, 255, 255, 0.18);
  }
  .sin:focus {
    border-bottom-color: rgba(236, 216, 143, 0.55);
  }
  .sunit {
    font-size: 9px;
    color: var(--text-ghost);
    flex-shrink: 0;
  }
  .chips {
    display: flex;
    gap: 3px;
    flex-wrap: wrap;
  }
  .chip {
    background: var(--bg-surface);
    border: none;
    border-radius: 999px;
    box-shadow: var(--ring);
    color: var(--text-dim);
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 8.5px;
    font-weight: 600;
    padding: 2px 7px;
    cursor: pointer;
    font-variant-numeric: tabular-nums;
    transition: color 0.15s ease, background 0.15s ease, box-shadow 0.15s ease;
  }
  .chip:hover {
    color: var(--text-primary);
  }
  .chip.on {
    color: #ecd88f;
    box-shadow:
      var(--ring),
      inset 0 0 0 1px rgba(236, 216, 143, 0.45);
  }
  /* per-auton field picker — same rows as Settings' own picker, restyled to
     this panel's chip idiom (amber outline rather than Settings' solid fill) */
  .fieldrows {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }
  .glabel {
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 0.04em;
    text-transform: uppercase;
    color: var(--text-ghost);
    margin: 6px 0 0 2px;
  }
  .frow {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 8px;
    background: var(--bg-raised);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 4px 5px 4px 9px;
    min-height: 30px;
  }
  .fname {
    font-size: 11px;
    font-weight: 600;
    color: var(--text-secondary);
  }
  .frow.solo {
    border: none;
    color: var(--text-dim);
    font-family: inherit;
    font-size: 11px;
    font-weight: 600;
    text-align: left;
    cursor: pointer;
    padding: 7px 9px;
    transition: color 0.15s ease, box-shadow 0.15s ease;
  }
  .frow.solo:hover {
    color: var(--text-primary);
  }
  .frow.solo.on {
    color: #ecd88f;
    box-shadow:
      var(--ring),
      inset 0 0 0 1px rgba(236, 216, 143, 0.45);
  }
  .fvars {
    display: flex;
    gap: 4px;
  }
  .fvar {
    background: var(--bg-surface);
    border: none;
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    color: var(--text-dim);
    font-family: inherit;
    font-size: 10px;
    font-weight: 600;
    padding: 4px 8px;
    cursor: pointer;
    transition: color 0.15s ease, box-shadow 0.15s ease;
  }
  .fvar:hover {
    color: var(--text-primary);
  }
  .fvar.on {
    color: #ecd88f;
    box-shadow:
      var(--ring),
      inset 0 0 0 1px rgba(236, 216, 143, 0.45);
  }

  .subhdr {
    font-size: 9px;
    font-weight: 700;
    color: var(--text-secondary);
    display: flex;
    align-items: baseline;
    gap: 6px;
    margin-top: 2px;
  }
  .mono2 {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 8px;
    font-weight: 400;
    color: var(--text-ghost);
  }

  /* Grip / Omni segmented toggle */
  .seg {
    display: flex;
    gap: 2px;
    background: var(--bg-base);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 2px;
  }
  .seg button {
    background: none;
    border: none;
    color: var(--text-dim);
    font-family: inherit;
    font-size: 9px;
    font-weight: 600;
    padding: 2px 10px;
    border-radius: 4px;
    cursor: pointer;
    transition: background 0.15s ease, color 0.15s ease;
  }
  .seg button:hover {
    color: var(--text-primary);
  }
  .seg button.on {
    background: rgba(255, 255, 255, 0.1);
    color: var(--text-primary);
  }

  .kv {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    font-size: 9.5px;
    color: var(--text-secondary);
  }
  .mono {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9px;
    color: var(--text-dim);
    font-variant-numeric: tabular-nums;
  }
  .mono.big {
    font-size: 13px;
    font-weight: 700;
    color: var(--text-primary);
  }
  .sel {
    box-shadow:
      var(--ring),
      inset 0 0 0 1px rgba(236, 216, 143, 0.25);
  }
</style>
