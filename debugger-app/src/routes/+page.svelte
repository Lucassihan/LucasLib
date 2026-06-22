<script lang="ts">
  import TopBar from '$lib/components/TopBar.svelte';
  import FieldPanel from '$lib/components/FieldPanel.svelte';
  import GraphPanel from '$lib/components/GraphPanel.svelte';
  import Timeline from '$lib/components/Timeline.svelte';
  import MovementInspector from '$lib/components/MovementInspector.svelte';
  import PidReadout from '$lib/components/PidReadout.svelte';
  import LyapunovReadout from '$lib/components/LyapunovReadout.svelte';
  import ControlPipeline from '$lib/components/ControlPipeline.svelte';
  import SettingsPanel from '$lib/components/SettingsPanel.svelte';
  import LibraryPanel from '$lib/components/LibraryPanel.svelte';
  import { traceState, movementAt } from '$lib/stores/trace.svelte';
  import { settings } from '$lib/stores/settings.svelte';
  import { PID_TERM, isLyPose } from '$lib/trace/labels';
  import type { Tick } from '$lib/trace/types';

  let settingsOpen = $state(false);
  let libraryOpen = $state(false);

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

  // normal: single governed-output line per channel
  const driveNormal = $derived([
    { data: activeTicks.map((t) => ({ t: t.t, v: t.drivePow })), color: '#3b82f6' }
  ]);
  const turnNormal = $derived([
    { data: activeTicks.map((t) => ({ t: t.t, v: turnVal(t) })), color: '#ef4444' }
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
          { data: activeTicks.map((t) => ({ t: t.t, v: t.turnOut - lyLat(t) })), color: '#fb923c' },
          { data: activeTicks.map((t) => ({ t: t.t, v: lyLat(t) })), color: '#2dd4bf' }
        ]
      : []
  );

  const driveSeries = $derived(settings.showBreakdown ? driveBreakdown : driveNormal);
  // breakdown ON: lyPose shows prop + Lyapunov terms; others show P/I/D.
  // breakdown OFF: single governed-output line for all (lyPose = total ω).
  const turnSeries = $derived(
    settings.showBreakdown ? (lyActive ? lyOmegaSeries : turnBreakdown) : turnNormal
  );

  const pidLegend = [
    { label: 'P', color: PID_TERM.p },
    { label: 'I', color: PID_TERM.i },
    { label: 'D', color: PID_TERM.d }
  ];
  const lyLegend = [
    { label: 'P', color: '#fb923c' },
    { label: 'Lyap', color: '#2dd4bf' }
  ];

  const driveActive = $derived(driveNormal[0].data.some((d) => Math.abs(d.v) > 0.1));
  const turnActive = $derived(turnNormal[0].data.some((d) => Math.abs(d.v) > 0.1));
</script>

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
      <Timeline />
    </div>

    <div class="graphs-col">
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
</style>
