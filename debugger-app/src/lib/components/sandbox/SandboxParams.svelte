<script lang="ts">
  import { sandbox, getRoute, getTrajectory, ensureTrajectory, setTrajParam, DEFAULT_TRAJ_PARAMS } from '$lib/stores/sandbox.svelte';
  import { deriveMotion } from '$lib/sandbox/sim';
  import { prettyMovement } from '$lib/trace/labels';
  import SliderRow from './SliderRow.svelte';

  const route = $derived(getRoute());

  // selected [TRAJECTORY] → its follow settings (defaults until first edited)
  const selTraj = $derived.by(() => {
    if (sandbox.selected === null) return null;
    if (route.parsed.stmts[sandbox.selected]?.kind !== 'trajectory') return null;
    const seg = route.sim.segments.find((s) => s.stmtIndex === sandbox.selected);
    const params = getTrajectory(sandbox.selected)?.params ?? DEFAULT_TRAJ_PARAMS;
    return { stmtIndex: sandbox.selected, seg, params };
  });
  function setTraj(key: 'maxVel' | 'exitVel' | 'maxAccel' | 'beta' | 'zeta', v: number) {
    if (!selTraj?.seg) return;
    ensureTrajectory(selTraj.stmtIndex, selTraj.seg.start);
    setTrajParam(selTraj.stmtIndex, key, v);
  }
  // motion limits derived from the drivetrain spec (shown as a readout)
  const motion = $derived(deriveMotion(sandbox.knobs));
  const selSeg = $derived(
    sandbox.selected !== null ? route.sim.segments.find((s) => s.stmtIndex === sandbox.selected) ?? null : null
  );

  // per-movement heading-correction effort for the selected moveTo / driveF/B
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
  {#if selTraj}
    <section class="panel group traj-group">
      <div class="gtitle">Trajectory <span class="gsub">follow settings for this [TRAJECTORY]</span></div>
      <SliderRow label="max velocity" min={5} max={90} step={1} unit="in/s" value={selTraj.params.maxVel} onchange={(v) => setTraj('maxVel', v)} />
      <SliderRow label="exit velocity" min={0} max={90} step={1} unit="in/s" value={selTraj.params.exitVel} onchange={(v) => setTraj('exitVel', v)} />
      <SliderRow label="max acceleration" min={10} max={300} step={5} unit="in/s²" value={selTraj.params.maxAccel} onchange={(v) => setTraj('maxAccel', v)} />
      <SliderRow label="RAMSETE β" min={0} max={10} step={0.05} decimals={2} value={selTraj.params.beta} onchange={(v) => setTraj('beta', v)} />
      <SliderRow label="RAMSETE ζ" min={0.05} max={1} step={0.05} decimals={2} value={selTraj.params.zeta} onchange={(v) => setTraj('zeta', v)} />
      <div class="note">Drag anchors + tangent handles on the field to shape the spline; double-click the curve to insert a point. The first end stays pinned to the robot's pose here.</div>
    </section>
  {/if}
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
    <SliderRow label="Weight" min={2} max={10} step={0.25} unit="lb" value={sandbox.knobs.robotWeight} onchange={(v) => (sandbox.knobs.robotWeight = Math.max(2, v))} />
    <div class="note derived">
      → top <b>{fmt(motion.topLinear, 0)} in/s</b> · <b>{fmt(motion.topAngular, 0)}°/s</b> · accel
      <b>{fmt(motion.linAccel, 0)} in/s²</b> · <b>{fmt(motion.angAccel, 0)}°/s²</b>
    </div>
    <div class="note">
      Base weight 2 lb. Angular numbers use the trackwidth from robot settings (width − 3 in =
      {fmt(sandbox.knobs.trackwidth)}).
    </div>
  </section>

  <section class="panel group">
    <div class="gtitle">PID gains <span class="gsub">movement aggressiveness</span></div>

    <SliderRow label="drive kP" min={0} max={2} step={0.005} decimals={3} unit="V/in" value={sandbox.knobs.driveKp} onchange={(v) => (sandbox.knobs.driveKp = v)} />
    <SliderRow label="turn kP" min={0} max={2} step={0.005} decimals={3} unit="V/°" value={sandbox.knobs.turnKp} onchange={(v) => (sandbox.knobs.turnKp = v)} />
    <SliderRow label="swing kP" min={0} max={2} step={0.005} decimals={3} unit="V/°" value={sandbox.knobs.swingKp} onchange={(v) => (sandbox.knobs.swingKp = v)} />
    <SliderRow label="heading kP" min={0} max={2} step={0.005} decimals={3} unit="V/°" value={sandbox.knobs.headingKp} onchange={(v) => (sandbox.knobs.headingKp = v)} />
    <div class="note">
      How hard each channel pushes on its error. Turns and swings still never overshoot — the kP shapes the
      approach under the braking limit. Heading kP scales the laterals' correction (× the per-movement effort).
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
      Early-exit a turn into a moveTo and the leftover spin swings through the first split second; early-exit
      a lateral into a facePoint and the robot keeps translating through the turn. Omni wheels barely resist
      that slide (much more pronounced); grip wheels arc it with the robot and shed it fast.
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
        Tuning the SELECTED movement only — click another movement to tune that one.
        {#if sandbox.efforts[selSeg.stmtIndex] !== undefined}
          <button class="mini" onclick={clearSelEffort}>reset to default ({fmt(effortDefault, 2)})</button>
        {/if}
      </div>
    {:else}
      <div class="note">Click a moveTo / driveForward / driveBackward on the field to tune how fast IT corrects heading. Defaults below apply to untuned movements.</div>
    {/if}

    <SliderRow label="moveTo default" min={0} max={2} step={0.01} decimals={2} value={sandbox.knobs.moveToHeadingEffort} onchange={(v) => (sandbox.knobs.moveToHeadingEffort = v)} />
    <SliderRow label="driveF/B default" min={0} max={2} step={0.01} decimals={2} value={sandbox.knobs.driveHeadingEffort} onchange={(v) => (sandbox.knobs.driveHeadingEffort = v)} />
  </section>

  <section class="panel group">
    <div class="gtitle">Library globals <span class="gsub">real tunables, set outside calls</span></div>

    <SliderRow label="headingDisableDist" min={0} max={12} step={0.25} unit="in" value={sandbox.knobs.headingDisableDist} onchange={(v) => (sandbox.knobs.headingDisableDist = v)} />
    <SliderRow label="kLat (lyPose)" min={0} max={0.6} step={0.01} decimals={2} value={sandbox.knobs.kLat} onchange={(v) => (sandbox.knobs.kLat = v)} />
    <SliderRow label="lyPose turn-off dist" min={0} max={8} step={0.25} unit="in" value={sandbox.knobs.lyPoseTurnDisableLength} onchange={(v) => (sandbox.knobs.lyPoseTurnDisableLength = v)} />
  </section>

  <section class="panel group">
    <div class="gtitle">Default exit conditions <span class="gsub">for ec fields you leave out</span></div>

    <div class="subhdr">Lateral <span class="mono2">LateralExitConditions</span></div>
    <SliderRow label="tOut" slider={false} min={0} max={60000} step={50} decimals={0} unit="ms" value={sandbox.knobs.ecLatTOut} onchange={(v) => (sandbox.knobs.ecLatTOut = v)} />
    <SliderRow label="setR" slider={false} min={0} max={144} step={0.1} decimals={2} unit="in" value={sandbox.knobs.ecLatSetR} onchange={(v) => (sandbox.knobs.ecLatSetR = v)} />
    <SliderRow label="setD" slider={false} min={0} max={100} step={0.001} decimals={3} unit="in/ms" value={sandbox.knobs.ecLatSetD} onchange={(v) => (sandbox.knobs.ecLatSetD = v)} />
    <SliderRow label="crossD" slider={false} min={-1} max={144} step={0.1} decimals={2} unit="in" value={sandbox.knobs.ecLatCrossD} onchange={(v) => (sandbox.knobs.ecLatCrossD = v)} />

    <div class="subhdr">Turn <span class="mono2">TurnExitConditions</span></div>
    <SliderRow label="tOut" slider={false} min={0} max={60000} step={50} decimals={0} unit="ms" value={sandbox.knobs.ecTurnTOut} onchange={(v) => (sandbox.knobs.ecTurnTOut = v)} />
    <SliderRow label="setR" slider={false} min={0} max={180} step={0.25} decimals={2} unit="°" value={sandbox.knobs.ecTurnSetR} onchange={(v) => (sandbox.knobs.ecTurnSetR = v)} />
    <SliderRow label="setD" slider={false} min={0} max={100} step={0.005} decimals={3} unit="°/ms" value={sandbox.knobs.ecTurnSetD} onchange={(v) => (sandbox.knobs.ecTurnSetD = v)} />
    <SliderRow label="extA" slider={false} min={0} max={180} step={0.25} decimals={2} unit="°" value={sandbox.knobs.ecTurnExtA} onchange={(v) => (sandbox.knobs.ecTurnExtA = v)} />
    <div class="note">
      setR = 0 turns settle off on either channel. crossD below 0 turns cross off (0 = the line sits on the target, higher
      slides it toward the robot to fire early); extA = 0 turns the ungated angle exit off.
      <br /><br />
      setD is Δerror per <em>millisecond</em>, so it saturates at the robot's own top speed — here
      <b>{fmt(motion.topLinear / 1000, 3)} in/ms</b> and <b>{fmt(motion.topAngular / 1000, 3)} °/ms</b>. Above that it can
      never fail and settle collapses to "within setR", which is why the lib's default of 2 does nothing.
      <br /><br />
      A value written in the code overrides its default here. Click any number to type it.
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
