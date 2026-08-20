<script lang="ts">
  // The two controller outputs, in volts — drivePow/headingPow for a PID
  // movement, the velocity layer's two channels for a trajectory. Blending,
  // governing and the motor diagram used to hang off the bottom of this card;
  // they live in the inspector's GovernorsCard and MotorDiagram now.
  import { traceState, movementAt, tickAt } from '$lib/stores/trace.svelte';
  import { isTrajectory } from '$lib/trace/labels';

  const m = $derived(movementAt(traceState.time));
  const tk = $derived(tickAt(traceState.time));

  const traj = $derived(m ? isTrajectory(m.type) : false);

  const fmt = (n: number, d = 2) => n.toFixed(d);
  const signed = (n: number) => `${n >= 0 ? '+' : '−'}${fmt(Math.abs(n))}`;

  // Raw-PID bars: simple magnitude fill from the left.
  const barPct = (v: number) => Math.min(Math.abs(v) / 12, 1) * 100;

  // The "turn" raw output depends on movement type: spot turns use turnPow,
  // swings use swingPow, lateral moves steer with the heading PID.
  function turnRaw(): { label: string; value: number } {
    if (!m || !tk) return { label: 'turnPow', value: 0 };
    if (m.type === 'face' || m.type === 'facePoint') return { label: 'turnPow', value: tk.turnPow };
    if (m.type.startsWith('swing')) return { label: 'swingPow', value: tk.swingPow };
    if (isTrajectory(m.type)) return { label: 'ω controller', value: tk.headingPow };
    return { label: 'headingPow', value: tk.headingPow };
  }

  const turn = $derived(turnRaw());
</script>

<section class="panel pipe">
  <div class="head">
    <span class="lbl">Control pipeline</span>
    <span class="sub">volts</span>
  </div>

  {#if m && tk}
    <div class="sec">{traj ? 'Velocity layer' : 'Raw PID'}</div>
    <div class="term">
      <span class="rl">{traj ? 'V controller' : 'drivePow'}</span>
      <div class="bar"><div class="fill" style="width: {barPct(tk.drivePow)}%; background: #3b82f6"></div></div>
      <span class="tv">{signed(tk.drivePow)}</span>
    </div>
    <div class="term">
      <span class="rl">{turn.label}</span>
      <div class="bar"><div class="fill" style="width: {barPct(turn.value)}%; background: #ef4444"></div></div>
      <span class="tv">{signed(turn.value)}</span>
    </div>

  {/if}
</section>

<style>
  .pipe {
    flex: 0 0 auto;
    padding: 12px 14px;
    display: flex;
    flex-direction: column;
    gap: 4px;
  }
  .head {
    display: flex;
    align-items: center;
    gap: 6px;
    margin-bottom: 4px;
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
  .sec {
    font-size: 9px;
    font-weight: 600;
    color: var(--text-muted);
    margin: 7px 0 3px;
  }
  .term {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 3px;
  }
  .rl {
    width: 64px;
    font-size: 10px;
    color: var(--text-dim);
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
    width: 50px;
    text-align: right;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9.5px;
    font-weight: 600;
    color: var(--text-primary);
  }

</style>
