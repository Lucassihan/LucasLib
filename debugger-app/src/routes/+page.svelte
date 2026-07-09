<script lang="ts">
  import TopBar from '$lib/components/TopBar.svelte';
  import FieldPanel from '$lib/components/FieldPanel.svelte';
  import GraphPanel from '$lib/components/GraphPanel.svelte';
  import MovementStrip from '$lib/components/MovementStrip.svelte';
  import Timeline from '$lib/components/Timeline.svelte';
  import MovementInspector from '$lib/components/MovementInspector.svelte';
  import PidReadout from '$lib/components/PidReadout.svelte';
  import LyapunovReadout from '$lib/components/LyapunovReadout.svelte';
  import ControlPipeline from '$lib/components/ControlPipeline.svelte';
  import MotorDiagram from '$lib/components/MotorDiagram.svelte';
  import SettingsPanel from '$lib/components/SettingsPanel.svelte';
  import LibraryPanel from '$lib/components/LibraryPanel.svelte';
  import { traceState, movementAt, tickAt, togglePlay, stepTicks, jumpMovement } from '$lib/stores/trace.svelte';
  import { settings } from '$lib/stores/settings.svelte';
  import { PID_TERM, COLORS, isLyPose, isVolDrive } from '$lib/trace/labels';
  import type { Tick } from '$lib/trace/types';

  let settingsOpen = $state(false);
  let libraryOpen = $state(false);

  // Global playback shortcuts (guide lives behind the ⌨ button in the timeline).
  // Inactive while a dialog is open or the focus is in a text field.
  function onKey(e: KeyboardEvent) {
    if (settingsOpen || libraryOpen) return;
    const t = e.target instanceof HTMLElement ? e.target : null;
    if (t && t.closest('input, textarea, select, [contenteditable="true"]')) return;
    // a focused button owns Space (avoid double-toggling via the play button)
    if (e.key === ' ' && t && t.closest('button')) return;
    switch (e.key) {
      case ' ':
        e.preventDefault();
        togglePlay();
        break;
      case 'ArrowLeft':
        e.preventDefault();
        stepTicks(e.shiftKey ? -10 : -1);
        break;
      case 'ArrowRight':
        e.preventDefault();
        stepTicks(e.shiftKey ? 10 : 1);
        break;
      case '[':
        jumpMovement(-1);
        break;
      case ']':
        jumpMovement(1);
        break;
      // ↑/↓ = start/end (Mac keyboards have no Home/End; those still work too)
      case 'ArrowUp':
      case 'Home':
        e.preventDefault();
        traceState.playing = false;
        traceState.time = 0;
        break;
      case 'ArrowDown':
      case 'End':
        e.preventDefault();
        traceState.playing = false;
        traceState.time = traceState.trace.duration;
        break;
    }
  }

  // Graphs scope to the movement under the scrubber.
  const active = $derived(movementAt(traceState.time));
  const activeTicks = $derived(
    active ? traceState.trace.ticks.filter((t) => t.mi === active.index) : []
  );
  const tStart = $derived(active ? active.startTime : 0);
  const tEnd = $derived(active ? active.endTime : 1);

  // which turn output is meaningful for this movement type
  function turnVal(t: Tick): number {
    if (!active) return 0;
    if (active.type === 'face' || active.type === 'facePoint') return t.turnPow;
    if (active.type.startsWith('swing')) return t.swingPow;
    return t.headingPow;
  }

  // lateral movements steer with the HEADING pid; turns use turn/swing pid
  const turnTitle = $derived.by(() => {
    if (!active) return 'Heading PID';
    if (active.type === 'face' || active.type === 'facePoint') return 'Turn PID';
    if (active.type.startsWith('swing')) return 'Swing PID';
    if (active.type === 'lyPose') return 'Lyapunov ω';
    return 'Heading PID';
  });

  const lyActive = $derived(!!active && isLyPose(active.type));
  // voldrive is open-loop: nothing changes within the call, so the right column
  // collapses to just the motor diagram (no readout / graphs / pipeline).
  const vdActive = $derived(!!active && isVolDrive(active.type));
  const curTick = $derived(tickAt(traceState.time));

  // normal: single governed-output line per channel
  const driveNormal = $derived([
    { data: activeTicks.map((t) => ({ t: t.t, v: t.drivePow })), color: COLORS.drive }
  ]);
  const turnNormal = $derived([
    { data: activeTicks.map((t) => ({ t: t.t, v: turnVal(t) })), color: COLORS.turn }
  ]);

  // breakdown: separate P / I / D term lines (kP·e, kI·∫e, kD·ė)
  const driveBreakdown = $derived(
    active
      ? [
          // lyPose's drive PID runs on along-line distance e_x, not lateralError
          { data: activeTicks.map((t) => ({ t: t.t, v: active.driveKp * (lyActive ? t.ex : t.lateralError) })), color: PID_TERM.p },
          { data: activeTicks.map((t) => ({ t: t.t, v: active.driveKi * t.driveI })), color: PID_TERM.i },
          { data: activeTicks.map((t) => ({ t: t.t, v: active.driveKd * t.driveD })), color: PID_TERM.d }
        ]
      : []
  );
  const turnBreakdown = $derived(
    active
      ? [
          { data: activeTicks.map((t) => ({ t: t.t, v: active.turnKp * t.headingError })), color: PID_TERM.p },
          { data: activeTicks.map((t) => ({ t: t.t, v: active.turnKi * t.turnI })), color: PID_TERM.i },
          { data: activeTicks.map((t) => ({ t: t.t, v: active.turnKd * t.turnD })), color: PID_TERM.d }
        ]
      : []
  );

  // lyPose ω graph: proportional (heading PID) term in orange + the Lyapunov
  // lateral-coupling term in the movement's turquoise.
  const D2R = Math.PI / 180;
  const sinc = (x: number) => (Math.abs(x) < 1e-3 ? 1 - (x * x) / 6 : Math.sin(x) / x);
  function lyLat(t: Tick): number {
    return -traceState.trace.header.kLat * t.driveOut * t.ey * sinc(t.headingError * D2R);
  }
  const lyOmegaSeries = $derived(
    lyActive
      ? [
          { data: activeTicks.map((t) => ({ t: t.t, v: t.turnOut - lyLat(t) })), color: COLORS.lyP },
          { data: activeTicks.map((t) => ({ t: t.t, v: lyLat(t) })), color: COLORS.lyLat }
        ]
      : []
  );

  const driveSeries = $derived(settings.showBreakdown ? driveBreakdown : driveNormal);
  // breakdown ON: lyPose shows prop + Lyapunov terms; others show P/I/D.
  // breakdown OFF: single governed-output line for all (lyPose = total ω).
  const turnSeries = $derived(
    settings.showBreakdown ? (lyActive ? lyOmegaSeries : turnBreakdown) : turnNormal
  );

  const pidLegend = $derived([
    { label: 'P', color: PID_TERM.p },
    { label: 'I', color: PID_TERM.i },
    { label: 'D', color: PID_TERM.d }
  ]);
  const lyLegend = $derived([
    { label: 'P', color: COLORS.lyP },
    { label: 'Lyap', color: COLORS.lyLat }
  ]);

  const driveActive = $derived(driveNormal[0].data.some((d) => Math.abs(d.v) > 0.1));
  const turnActive = $derived(turnNormal[0].data.some((d) => Math.abs(d.v) > 0.1));
</script>

<svelte:window onkeydown={onKey} />

<div class="app">
  <TopBar
    status={traceState.trace.header.autonName}
    onOpenSettings={() => (settingsOpen = true)}
    onOpenLibrary={() => (libraryOpen = true)}
  />

  <div class="workspace">
    <MovementInspector />

    <div class="field-col">
      <FieldPanel />
      <MovementStrip />
      <Timeline />
    </div>

    <div class="graphs-col">
      {#if vdActive}
        <!-- open-loop voltage: nothing varies within the call, so the column is
             just the motor diagram showing the fixed left/right command -->
        <section class="panel vd-panel">
          <div class="vd-head">
            <span class="vd-lbl">Motors</span>
            <span class="vd-sub">open loop · direct voltage</span>
          </div>
          <div class="vd-diagram">
            <MotorDiagram left={curTick ? curTick.leftOut : 0} right={curTick ? curTick.rightOut : 0} />
          </div>
        </section>
      {:else}
        {#if lyActive}
          <LyapunovReadout />
        {:else}
          <PidReadout />
        {/if}

        <GraphPanel
          title="Drive PID"
          grow={1.1}
          accent="#e8e8e8"
          active={driveActive}
          series={driveSeries}
          legend={settings.showBreakdown ? pidLegend : []}
          {tStart}
          {tEnd}
          playheadT={traceState.time}
        />
        <GraphPanel
          title={turnTitle}
          grow={1.1}
          accent="#e8e8e8"
          active={turnActive}
          series={turnSeries}
          legend={settings.showBreakdown ? (lyActive ? lyLegend : pidLegend) : []}
          {tStart}
          {tEnd}
          playheadT={traceState.time}
        />
        <ControlPipeline />
      {/if}
    </div>
  </div>
</div>

<SettingsPanel bind:open={settingsOpen} />
<LibraryPanel bind:open={libraryOpen} />

<style>
  .app {
    height: 100vh;
    display: flex;
    flex-direction: column;
    gap: var(--gap);
    padding: var(--gap);
    background: var(--bg-deep);
  }

  .workspace {
    flex: 1;
    display: flex;
    gap: var(--gap);
    min-height: 0;
  }

  /* middle: field map (largest) */
  .field-col {
    flex: 1.5;
    display: flex;
    flex-direction: column;
    gap: var(--gap);
    min-width: 0;
  }

  /* left: PID readout + graphs + pipeline */
  .graphs-col {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: var(--gap);
    min-width: 0;
  }

  /* voldrive: the whole column is one panel holding the motor diagram */
  .vd-panel {
    flex: 1;
    min-height: 0;
    padding: 14px 16px;
    display: flex;
    flex-direction: column;
    gap: 10px;
  }
  .vd-head {
    display: flex;
    align-items: baseline;
    gap: 8px;
  }
  .vd-lbl {
    font-size: 11px;
    font-weight: 700;
    color: var(--text-primary);
  }
  .vd-sub {
    font-size: 9px;
    color: var(--text-ghost);
  }
  .vd-diagram {
    flex: 1;
    min-height: 0;
    position: relative;
  }
</style>
