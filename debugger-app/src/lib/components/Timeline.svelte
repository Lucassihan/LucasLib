<script lang="ts">
  import { traceState, movementAt } from '$lib/stores/trace.svelte';
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

  function togglePlay() {
    if (traceState.time >= duration) traceState.time = 0;
    traceState.playing = !traceState.playing;
  }

  // Playback: advance scrub time by real elapsed ms while playing.
  $effect(() => {
    if (!traceState.playing) return;
    let raf = 0;
    let last = performance.now();
    const step = (now: number) => {
      traceState.time = Math.min(traceState.time + (now - last), duration);
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

<svelte:window onpointermove={onWinMove} onpointerup={onWinUp} />

{#if hoverTime !== null}
  <div class="scrub-tip" style="left: {hoverX}px; top: {hoverTop}px;">
    <span class="tip-move">{hoverMovement ? prettyMovement(hoverMovement.type) : '—'}</span>
    <span class="tip-time">{fmtPrecise(hoverTime)}</span>
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
