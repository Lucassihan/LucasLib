<script lang="ts">
  // Sandbox playback bar — same design language as the replay Timeline:
  // round play button, playback-speed popup, movement-colored track segments,
  // a time axis with nice ticks, scrub tooltip, and the mono clock.
  import { sandbox, getRoute, togglePlaySandbox } from '$lib/stores/sandbox.svelte';
  import { movementColor, prettyMovement } from '$lib/trace/labels';

  const route = $derived(getRoute());
  const duration = $derived(route.sim.duration || 1);

  let trackEl: HTMLElement | undefined = $state();
  let dragging = $state(false);
  let speed = $state(1);

  // Scrub tooltip (viewport coords; rendered position:fixed so it isn't clipped)
  let hoverTime = $state<number | null>(null);
  let hoverX = $state(0);
  let hoverTop = $state(0);

  const pct = (ms: number) => (ms / duration) * 100;

  // Playback: advance scrub time by elapsed ms × speed while playing.
  $effect(() => {
    if (!sandbox.playing) return;
    let raf = 0;
    let last = performance.now();
    const step = (now: number) => {
      sandbox.time = Math.min(sandbox.time + (now - last) * speed, duration);
      last = now;
      if (sandbox.time >= duration) {
        sandbox.playing = false;
        return;
      }
      raf = requestAnimationFrame(step);
    };
    raf = requestAnimationFrame(step);
    return () => cancelAnimationFrame(raf);
  });

  // playback-rate popup: presets + a draggable scale (replay Timeline design)
  const SPEED_PRESETS = [0.25, 0.5, 1];
  let speedOpen = $state(false);
  let speedRight = $state(0);
  let speedBottom = $state(0);
  function toggleSpeed(e: MouseEvent) {
    e.stopPropagation();
    if (!speedOpen) {
      const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
      speedRight = window.innerWidth - r.right;
      speedBottom = window.innerHeight - r.top + 8;
    }
    speedOpen = !speedOpen;
  }
  function setSpeed(v: number) {
    speed = Math.round(v * 100) / 100;
  }
  const speedLabel = $derived(speed < 1 ? `${speed}×`.replace('0.', '.') : `${speed}×`);

  function updateHover(clientX: number) {
    if (!trackEl) return;
    const rect = trackEl.getBoundingClientRect();
    const u = Math.max(0, Math.min(1, (clientX - rect.left) / rect.width));
    hoverTime = u * duration;
    hoverX = rect.left + u * rect.width;
    hoverTop = rect.top;
  }
  function onTrackMove(e: PointerEvent) {
    updateHover(e.clientX);
  }
  function onTrackLeave() {
    if (!dragging) hoverTime = null;
  }
  function onDown(e: PointerEvent) {
    dragging = true;
    sandbox.playing = false;
    updateHover(e.clientX);
    if (hoverTime !== null) sandbox.time = hoverTime;
  }
  function onWinMove(e: PointerEvent) {
    if (!dragging) return;
    updateHover(e.clientX);
    if (hoverTime !== null) sandbox.time = hoverTime;
  }
  function onWinUp() {
    if (dragging) {
      dragging = false;
      hoverTime = null;
    }
  }

  // time-axis tick generation (identical to the replay Timeline)
  function niceStep(rough: number): number {
    const pow = Math.pow(10, Math.floor(Math.log10(rough)));
    const f = rough / pow;
    let nf = 10;
    if (f < 1.5) nf = 1;
    else if (f < 3) nf = 2;
    else if (f < 7) nf = 5;
    return nf * pow;
  }
  const step = $derived(niceStep(duration / 5));
  const timeTicks = $derived.by(() => {
    const arr: number[] = [];
    for (let t = 0; t <= duration + 0.5; t += step) arr.push(t);
    return arr;
  });

  function fmtAxis(ms: number): string {
    const s = ms / 1000;
    return (step % 1000 === 0 ? s.toFixed(0) : s.toFixed(1)) + 's';
  }
  function fmtPrecise(ms: number): string {
    return (ms / 1000).toFixed(2) + 's';
  }
  function axisTransform(t: number): string {
    if (t <= 0) return 'translateX(0)';
    if (t >= duration) return 'translateX(-100%)';
    return 'translateX(-50%)';
  }

  interface Span {
    stmtIndex: number;
    fn: string;
    t0: number;
    t1: number;
  }
  const spans = $derived.by((): Span[] =>
    route.sim.segments
      .filter((s) => s.ticks.length > 0)
      .map((s) => ({ stmtIndex: s.stmtIndex, fn: s.fn, t0: s.ticks[0].t, t1: s.ticks[s.ticks.length - 1].t }))
  );

  const hoverSpan = $derived(
    hoverTime !== null ? spans.find((s) => hoverTime! >= s.t0 && hoverTime! <= s.t1) ?? null : null
  );
</script>

<svelte:window
  onpointermove={onWinMove}
  onpointerup={onWinUp}
  onpointerdown={() => (speedOpen = false)}
/>

{#if hoverTime !== null}
  <div class="scrub-tip" style="left: {hoverX}px; top: {hoverTop}px;">
    <span class="tip-move">{hoverSpan ? prettyMovement(hoverSpan.fn) : '—'}</span>
    <span class="tip-time">{fmtPrecise(hoverTime)}</span>
  </div>
{/if}

{#if speedOpen}
  <div class="speed-pop" style="right: {speedRight}px; bottom: {speedBottom}px;" onpointerdown={(e) => e.stopPropagation()} role="presentation">
    <div class="sp-title">Playback speed</div>
    <div class="sp-presets">
      {#each SPEED_PRESETS as s}
        <button class="sp-btn" class:on={speed === s} onclick={() => setSpeed(s)}>
          {s < 1 ? `${s}×`.replace('0.', '.') : `${s}×`}
        </button>
      {/each}
    </div>
    <div class="sp-slider">
      <input
        type="range"
        min="0"
        max="1"
        step="0.05"
        value={speed}
        oninput={(e) => setSpeed(parseFloat((e.currentTarget as HTMLInputElement).value))}
        aria-label="Playback speed scale"
      />
      <span class="sp-val">{speed.toFixed(2)}×</span>
    </div>
  </div>
{/if}

<section class="panel timeline">
  <button class="play" onclick={togglePlaySandbox} aria-label={sandbox.playing ? 'Pause' : 'Play'}>
    {#if sandbox.playing}
      <svg viewBox="0 0 12 12"><rect x="2.5" y="2" width="2.5" height="8" /><rect x="7" y="2" width="2.5" height="8" /></svg>
    {:else}
      <svg viewBox="0 0 12 12"><path d="M3 2 L10 6 L3 10 Z" /></svg>
    {/if}
  </button>

  <button class="speed" class:open={speedOpen} onclick={toggleSpeed} title="Playback speed" aria-label="Playback speed {speedLabel}">
    {speedLabel}
  </button>

  <div class="body">
    <div
      class="track"
      bind:this={trackEl}
      onpointerdown={onDown}
      onpointermove={onTrackMove}
      onpointerleave={onTrackLeave}
      role="slider"
      tabindex="0"
      aria-label="Sandbox timeline scrubber"
      aria-valuenow={Math.round(sandbox.time)}
      aria-valuemin="0"
      aria-valuemax={Math.round(duration)}
    >
      <div class="rail"></div>

      {#each spans as s, i (s.stmtIndex)}
        <div
          class="seg"
          class:selseg={sandbox.selected === s.stmtIndex}
          style="left: {pct(s.t0)}%; width: {Math.max(0.4, pct(s.t1 - s.t0))}%; background: {movementColor(s.fn)}"
        ></div>
        {#if i > 0}
          <div class="marker" style="left: {pct(s.t0)}%"></div>
        {/if}
      {/each}

      <div class="handle" style="left: {pct(Math.min(sandbox.time, duration))}%"></div>
    </div>

    <div class="axis">
      {#each timeTicks as t}
        <div class="axis-tick" style="left: {pct(t)}%">
          <span class="tick-mark"></span>
          <span class="tick-label" style="transform: {axisTransform(t)}">{fmtAxis(t)}</span>
        </div>
      {/each}
    </div>
  </div>

  <span class="clock">{fmtPrecise(sandbox.time)} <span class="clock-sep">/</span> {fmtPrecise(duration)}</span>
</section>

<style>
  .timeline {
    height: 58px;
    border-radius: var(--radius-bar);
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 0 14px;
  }

  .play {
    flex-shrink: 0;
    width: 22px;
    height: 22px;
    border: none;
    border-radius: 50%;
    background: #1a1a1a;
    display: flex;
    align-items: center;
    justify-content: center;
    cursor: pointer;
    transition: background 0.15s ease;
  }
  .play:hover {
    background: #1f1f1f;
  }
  .play svg {
    width: 11px;
    height: 11px;
    fill: #777;
  }

  .speed {
    flex-shrink: 0;
    width: 34px;
    border: none;
    border-radius: var(--radius-small);
    background: #1a1a1a;
    padding: 3px 0;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9px;
    font-weight: 600;
    color: var(--text-dim);
    cursor: pointer;
    transition: background 0.15s ease, color 0.15s ease;
  }
  .speed:hover,
  .speed.open {
    background: #1f1f1f;
    color: var(--text-primary);
  }

  .speed-pop {
    position: fixed;
    z-index: 60;
    display: flex;
    flex-direction: column;
    gap: 8px;
    width: 190px;
    padding: 10px 12px;
    background: var(--bg-raised);
    border-radius: var(--radius-small);
    box-shadow:
      var(--ring),
      0 6px 20px rgba(0, 0, 0, 0.45);
  }
  .sp-title {
    font-size: 9px;
    font-weight: 700;
    color: var(--text-secondary);
  }
  .sp-presets {
    display: flex;
    gap: 5px;
  }
  .sp-btn {
    flex: 1;
    border: none;
    border-radius: var(--radius-small);
    background: var(--bg-elevated);
    box-shadow: var(--ring);
    padding: 5px 0;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9.5px;
    font-weight: 600;
    color: var(--text-dim);
    cursor: pointer;
    transition: background 0.15s ease, color 0.15s ease, box-shadow 0.15s ease;
  }
  .sp-btn:hover {
    color: var(--text-primary);
  }
  .sp-btn.on {
    background: var(--accent-bg);
    box-shadow: inset 0 0 0 1px var(--accent);
    color: var(--accent-text);
  }
  .sp-slider {
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .sp-slider input[type='range'] {
    flex: 1;
    appearance: none;
    -webkit-appearance: none;
    height: 4px;
    border-radius: 2px;
    background: var(--bg-base);
    box-shadow: var(--ring);
    outline: none;
    cursor: pointer;
  }
  .sp-slider input[type='range']::-webkit-slider-thumb {
    appearance: none;
    -webkit-appearance: none;
    width: 11px;
    height: 11px;
    border-radius: 50%;
    background: #cfcfcf;
    box-shadow: 0 1px 3px rgba(0, 0, 0, 0.6);
  }
  .sp-slider input[type='range']::-moz-range-thumb {
    width: 11px;
    height: 11px;
    border: none;
    border-radius: 50%;
    background: #cfcfcf;
    box-shadow: 0 1px 3px rgba(0, 0, 0, 0.6);
  }
  .sp-val {
    width: 40px;
    text-align: right;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9px;
    font-weight: 600;
    color: var(--text-dim);
    font-variant-numeric: tabular-nums;
  }

  .clock {
    flex-shrink: 0;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9px;
    font-weight: 500;
    color: var(--text-dim);
    font-variant-numeric: tabular-nums;
    white-space: nowrap;
  }
  .clock-sep {
    color: var(--text-ghost);
  }

  .body {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 3px;
  }

  .track {
    position: relative;
    height: 20px;
    cursor: pointer;
  }

  .rail {
    position: absolute;
    top: 50%;
    left: 0;
    right: 0;
    height: 5px;
    border-radius: 2.5px;
    background: var(--bg-base);
    box-shadow: var(--ring);
    transform: translateY(-50%);
  }

  .seg {
    position: absolute;
    top: 50%;
    height: 5px;
    border-radius: 2.5px;
    transform: translateY(-50%);
    opacity: 0.85;
  }
  .seg.selseg {
    height: 8px;
    opacity: 1;
  }

  .marker {
    position: absolute;
    top: 50%;
    width: 1px;
    height: 13px;
    background: var(--border-light);
    transform: translate(-0.5px, -50%);
  }

  .handle {
    position: absolute;
    top: 50%;
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: #999;
    transform: translate(-50%, -50%);
    z-index: 2;
  }

  .axis {
    position: relative;
    height: 12px;
  }
  .axis-tick {
    position: absolute;
    top: 0;
  }
  .tick-mark {
    position: absolute;
    top: 0;
    left: 0;
    width: 1px;
    height: 3px;
    background: var(--border-subtle);
  }
  .tick-label {
    position: absolute;
    top: 4px;
    font-size: 7px;
    font-weight: 400;
    color: var(--text-ghost);
    white-space: nowrap;
  }

  .scrub-tip {
    position: fixed;
    z-index: 50;
    transform: translate(-50%, calc(-100% - 10px));
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 1px;
    padding: 5px 9px;
    background: var(--bg-raised);
    border-radius: var(--radius-small);
    box-shadow:
      var(--ring),
      0 6px 20px rgba(0, 0, 0, 0.45);
    pointer-events: none;
    white-space: nowrap;
  }
  .tip-move {
    font-size: 11px;
    font-weight: 600;
    color: var(--text-primary);
  }
  .tip-time {
    font-size: 9px;
    font-weight: 400;
    color: var(--text-dim);
  }
</style>
