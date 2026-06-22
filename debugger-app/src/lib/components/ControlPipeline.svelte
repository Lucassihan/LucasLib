<script lang="ts">
  // Universal control pipeline: raw PID outputs -> blending (cosine scaling +
  // turn priority) + governing -> governed motor volts (the tank-drive diagram).
  // Shown for every movement. The boomerang-only slip-cap throttle lives in the
  // inspector.
  import { traceState, movementAt, tickAt, tickPairAt } from '$lib/stores/trace.svelte';
  import { settings } from '$lib/stores/settings.svelte';
  import { isTurn } from '$lib/trace/labels';
  import { governConfig, activeGovernors, scopeLabel } from '$lib/trace/governor';
  import MotorDiagram from './MotorDiagram.svelte';

  const m = $derived(movementAt(traceState.time));
  const tk = $derived(tickAt(traceState.time));
  const turning = $derived(m ? isTurn(m.type) : false);

  // Live governor activation for the scope selected in the inspector's toggle.
  const scope = $derived(settings.governScope);
  const govCfg = $derived(governConfig(traceState.trace.header, scope));
  const govPair = $derived(tickPairAt(traceState.time));
  const govFlags = $derived(activeGovernors(govPair.cur, govPair.prev, govCfg, scope));

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
    return { label: 'headingPow', value: tk.headingPow };
  }

  const turn = $derived(turnRaw());
  // Cosine multiplier applied to drive in blendOutputs (only when enabled).
  const cosMult = $derived(tk ? Math.cos((tk.headingError * Math.PI) / 180) : 1);
</script>

<section class="panel pipe">
  <div class="head">
    <span class="lbl">Control pipeline</span>
    <span class="sub">volts</span>
  </div>

  {#if m && tk}
    <div class="sec">Raw PID</div>
    <div class="term">
      <span class="rl">drivePow</span>
      <div class="bar"><div class="fill" style="width: {barPct(tk.drivePow)}%; background: #3b82f6"></div></div>
      <span class="tv">{signed(tk.drivePow)}</span>
    </div>
    <div class="term">
      <span class="rl">{turn.label}</span>
      <div class="bar"><div class="fill" style="width: {barPct(turn.value)}%; background: #ef4444"></div></div>
      <span class="tv">{signed(turn.value)}</span>
    </div>

    <div class="cols">
      <div class="lcol">
        <div class="blend" class:dim={turning}>
          <div class="sec">Blending {#if turning}<span class="na">n/a · turn</span>{/if}</div>
          <div class="kv">
            <span class="k">cos(eθ)</span>
            <span class="v">{fmt(cosMult)}
              {#if m.cosineScaling}<span class="on">ON</span>{:else}<span class="off">OFF</span>{/if}
            </span>
          </div>
          <div class="kv"><span class="k">turn priority</span><span class="v">{fmt(m.turnPriority)}</span></div>
        </div>

        <div class="sec">Governing <span class="scope">{scopeLabel(scope)}</span></div>
        <div class="gov-flags">
          <span class="gf" class:hot={govFlags.min}>min</span>
          <span class="gf" class:hot={govFlags.max}>max</span>
          <span class="gf" class:hot={govFlags.accel}>accel</span>
          <span class="gf" class:hot={govFlags.decel}>decel</span>
        </div>
      </div>

      <div class="rcol">
        <div class="sec">Motors (governed)</div>
        <div class="diagram-wrap">
          <MotorDiagram left={tk.leftOut} right={tk.rightOut} />
        </div>
      </div>
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

  .cols {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 14px;
    align-items: stretch;
    margin-top: 2px;
  }
  .lcol {
    min-width: 0;
  }
  .rcol {
    min-width: 0;
    display: flex;
    flex-direction: column;
  }
  .diagram-wrap {
    flex: 1;
    min-height: 0;
    position: relative;
  }

  .kv {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    font-size: 10px;
    margin: 3px 0;
    gap: 6px;
  }
  .k {
    color: var(--text-dim);
  }
  .v {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-weight: 600;
    color: var(--text-primary);
    white-space: nowrap;
  }
  .on {
    font-size: 8px;
    font-weight: 700;
    color: #0c2c12;
    background: #34d399;
    border-radius: 3px;
    padding: 1px 5px;
    margin-left: 5px;
  }
  .off {
    font-size: 8px;
    font-weight: 700;
    color: var(--text-muted);
    background: var(--bg-base);
    border-radius: 3px;
    padding: 1px 5px;
    margin-left: 5px;
  }

  .blend.dim {
    opacity: 0.4;
  }
  .na {
    font-size: 8px;
    font-weight: 400;
    color: var(--text-ghost);
    letter-spacing: 0;
  }
  .scope {
    font-size: 8px;
    font-weight: 600;
    color: var(--text-secondary);
    background: var(--bg-base);
    border-radius: 3px;
    padding: 1px 5px;
    margin-left: 4px;
  }
  .gov-flags {
    display: flex;
    gap: 4px;
    margin-top: 2px;
  }
  .gf {
    flex: 1;
    text-align: center;
    font-size: 8px;
    font-weight: 600;
    color: var(--text-ghost);
    background: var(--bg-base);
    border-radius: 3px;
    padding: 3px 0;
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.03);
  }
  .gf.hot {
    color: #3a1414;
    background: #f87171;
    box-shadow: none;
  }
</style>
