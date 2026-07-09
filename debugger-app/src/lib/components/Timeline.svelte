<script lang="ts">
  import { traceState, movementAt, togglePlay } from '$lib/stores/trace.svelte';
  import { prettyMovement, movementColor } from '$lib/trace/labels';

  const duration = $derived(traceState.trace.duration || 1);
  const movements = $derived(traceState.trace.movements);

  let trackEl: HTMLElement | undefined = $state();
  let dragging = $state(false);

  // Scrub tooltip state (position in viewport coords; rendered position:fixed
  // so it isn't clipped by the panel's overflow).
  let hoverTime = $state<number | null>(null);
  let hoverX = $state(0);
  let hoverTop = $state(0);

  const pct = (ms: number) => (ms / duration) * 100;

  // Playback: advance scrub time by elapsed ms × speed while playing.
  $effect(() => {
    if (!traceState.playing) return;
    let raf = 0;
    let last = performance.now();
    const step = (now: number) => {
      traceState.time = Math.min(traceState.time + (now - last) * traceState.speed, duration);
      last = now;
      if (traceState.time >= duration) {
        traceState.playing = false;
        return;
      }
      raf = requestAnimationFrame(step);
    };
    raf = requestAnimationFrame(step);
    return () => cancelAnimationFrame(raf);
  });

  // playback-rate popup: presets + a draggable scale
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
    traceState.speed = Math.round(v * 100) / 100;
  }
  const speedLabel = $derived(
    traceState.speed < 1 ? `${traceState.speed}×`.replace('0.', '.') : `${traceState.speed}×`
  );

  // keyboard-shortcuts guide popover, anchored above its toggle button
  let guideOpen = $state(false);
  let guideRight = $state(0);
  let guideBottom = $state(0);
  function toggleGuide(e: MouseEvent) {
    e.stopPropagation();
    if (!guideOpen) {
      const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
      guideRight = window.innerWidth - r.right;
      guideBottom = window.innerHeight - r.top + 8;
    }
    guideOpen = !guideOpen;
  }
  const SHORTCUTS = [
    { keys: ['Space'], what: 'Play / pause' },
    { keys: ['←', '→'], what: 'Step one tick · Shift = 10' },
    { keys: ['['], what: 'Movement start / previous' },
    { keys: [']'], what: 'Next movement' },
    { keys: ['↑', '↓'], what: 'Start / end of run' }
  ];

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
    traceState.playing = false;
    updateHover(e.clientX);
    if (hoverTime !== null) traceState.time = hoverTime;
  }
  function onWinMove(e: PointerEvent) {
    if (!dragging) return;
    updateHover(e.clientX);
    if (hoverTime !== null) traceState.time = hoverTime;
  }
  function onWinUp() {
    if (dragging) {
      dragging = false;
      hoverTime = null;
    }
  }

  // time-axis tick generation
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

  const hoverMovement = $derived(hoverTime !== null ? movementAt(hoverTime) : null);
</script>

<svelte:window
  onpointermove={onWinMove}
  onpointerup={onWinUp}
  onpointerdown={() => {
    guideOpen = false;
    speedOpen = false;
  }}
/>

{#if hoverTime !== null}
  <div class="scrub-tip" style="left: {hoverX}px; top: {hoverTop}px;">
    <span class="tip-move">{hoverMovement ? prettyMovement(hoverMovement.type) : '—'}</span>
    <span class="tip-time">{fmtPrecise(hoverTime)}</span>
  </div>
{/if}

{#if speedOpen}
  <div class="speed-pop" style="right: {speedRight}px; bottom: {speedBottom}px;" onpointerdown={(e) => e.stopPropagation()} role="presentation">
    <div class="sp-title">Playback speed</div>
    <div class="sp-presets">
      {#each SPEED_PRESETS as s}
        <button class="sp-btn" class:on={traceState.speed === s} onclick={() => setSpeed(s)}>
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
        value={traceState.speed}
        oninput={(e) => setSpeed(parseFloat((e.currentTarget as HTMLInputElement).value))}
        aria-label="Playback speed scale"
      />
      <span class="sp-val">{traceState.speed.toFixed(2)}×</span>
    </div>
  </div>
{/if}

{#if guideOpen}
  <div class="kbd-pop" style="right: {guideRight}px; bottom: {guideBottom}px;" onpointerdown={(e) => e.stopPropagation()} role="presentation">
    <div class="kbd-title">Keyboard shortcuts</div>
    {#each SHORTCUTS as s}
      <div class="kbd-row">
        <span class="kbd-keys">
          {#each s.keys as k}<kbd>{k}</kbd>{/each}
        </span>
        <span class="kbd-what">{s.what}</span>
      </div>
    {/each}
  </div>
{/if}

<section class="panel timeline">
  <button class="play" onclick={togglePlay} aria-label={traceState.playing ? 'Pause' : 'Play'}>
    {#if traceState.playing}
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
      aria-label="Timeline scrubber"
      aria-valuenow={Math.round(traceState.time)}
      aria-valuemin="0"
      aria-valuemax={Math.round(duration)}
    >
      <div class="rail"></div>

      {#each movements as m, i (m.index)}
        <div
          class="seg"
          style="left: {pct(m.startTime)}%; width: {pct(m.endTime - m.startTime)}%; background: {movementColor(m.type)}"
        ></div>
        {#if i > 0}
          <div class="marker" style="left: {pct(m.startTime)}%"></div>
        {/if}
      {/each}

      <div class="handle" style="left: {pct(traceState.time)}%"></div>
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

  <span class="clock">{fmtPrecise(traceState.time)} <span class="clock-sep">/</span> {fmtPrecise(duration)}</span>

  <button class="kbd-btn" class:open={guideOpen} onclick={toggleGuide} title="Keyboard shortcuts" aria-label="Keyboard shortcuts">
    <svg viewBox="0 0 20 12" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round">
      <rect x="0.8" y="0.8" width="18.4" height="10.4" rx="1.6" />
      <path d="M3.4 3.6h.01M6.6 3.6h.01M9.8 3.6h.01M13 3.6h.01M16.2 3.6h.01M3.4 6h.01M6.6 6h.01M9.8 6h.01M13 6h.01M16.2 6h.01M5.5 8.6h9" />
    </svg>
  </button>
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

  /* speed popup — presets + draggable scale */
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

  .kbd-btn {
    flex-shrink: 0;
    display: flex;
    align-items: center;
    background: none;
    border: none;
    padding: 3px;
    color: var(--text-ghost);
    cursor: pointer;
    transition: color 0.15s ease;
  }
  .kbd-btn:hover,
  .kbd-btn.open {
    color: var(--text-primary);
  }
  .kbd-btn svg {
    width: 18px;
    height: 11px;
  }

  /* shortcuts guide — fixed so it floats above the panel without clipping */
  .kbd-pop {
    position: fixed;
    z-index: 60;
    display: flex;
    flex-direction: column;
    gap: 5px;
    padding: 10px 12px;
    background: var(--bg-raised);
    border-radius: var(--radius-small);
    box-shadow:
      var(--ring),
      0 6px 20px rgba(0, 0, 0, 0.45);
  }
  .kbd-title {
    font-size: 9px;
    font-weight: 700;
    color: var(--text-secondary);
    margin-bottom: 2px;
  }
  .kbd-row {
    display: flex;
    align-items: center;
    gap: 10px;
  }
  .kbd-keys {
    display: flex;
    gap: 3px;
    min-width: 78px;
  }
  .kbd-pop kbd {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 8.5px;
    font-weight: 600;
    color: var(--text-primary);
    background: var(--bg-elevated);
    border-radius: 3px;
    padding: 1.5px 5px;
    box-shadow:
      inset 0 -1px 0 rgba(0, 0, 0, 0.55),
      inset 0 0 0 1px rgba(255, 255, 255, 0.07);
  }
  .kbd-what {
    font-size: 9px;
    color: var(--text-dim);
    white-space: nowrap;
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

  /* segments colored by movement type (background set inline) */
  .seg {
    position: absolute;
    top: 50%;
    height: 5px;
    border-radius: 2.5px;
    transform: translateY(-50%);
    opacity: 0.85;
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

  /* scrub tooltip — fixed so it floats above the panel without clipping */
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
