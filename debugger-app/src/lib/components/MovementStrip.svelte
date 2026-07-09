<script lang="ts">
  // Filmstrip of movement chips between the field and the timeline.
  // Click a chip → playhead jumps to that movement's start (play state kept).
  // The active chip follows the playhead and stays scrolled into view.
  import { traceState, movementAt } from '$lib/stores/trace.svelte';
  import { prettyMovement, movementColor } from '$lib/trace/labels';

  const movements = $derived(traceState.trace.movements);
  const active = $derived(movementAt(traceState.time));

  let stripEl: HTMLElement | undefined = $state();

  function jump(startTime: number) {
    traceState.time = startTime;
  }

  // keep the active chip visible while the playhead moves
  $effect(() => {
    const idx = active?.index;
    if (idx === undefined || !stripEl) return;
    const chip = stripEl.querySelector<HTMLElement>(`[data-mi="${idx}"]`);
    chip?.scrollIntoView({ block: 'nearest', inline: 'nearest', behavior: 'smooth' });
  });
</script>

<section class="panel strip-panel">
  <div class="strip" bind:this={stripEl}>
    {#each movements as m (m.index)}
      <button
        class="chip"
        class:on={active?.index === m.index}
        data-mi={m.index}
        onclick={() => jump(m.startTime)}
        title="Jump to {prettyMovement(m.type)} · {(m.startTime / 1000).toFixed(2)}s"
      >
        <span class="cdot" style="background: {movementColor(m.type)}"></span>
        <span class="cname">{prettyMovement(m.type)}</span>
        <span class="cdur">{((m.endTime - m.startTime) / 1000).toFixed(1)}s</span>
      </button>
    {/each}
  </div>
</section>

<style>
  .strip-panel {
    flex-shrink: 0;
    border-radius: var(--radius-bar);
    padding: 6px 10px;
  }

  .strip {
    display: flex;
    gap: 6px;
    overflow-x: auto;
    scrollbar-width: none;
  }
  .strip::-webkit-scrollbar {
    display: none;
  }

  .chip {
    flex-shrink: 0;
    display: flex;
    align-items: center;
    gap: 6px;
    border: none;
    cursor: pointer;
    background: var(--bg-elevated);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 5px 10px;
    font-family: inherit;
    font-size: 10px;
    color: var(--text-dim);
    white-space: nowrap;
    transition:
      background 0.15s ease,
      box-shadow 0.15s ease,
      color 0.15s ease;
  }
  .chip:hover {
    color: var(--text-primary);
    background: var(--bg-raised);
  }
  .chip.on {
    background: var(--accent-bg);
    box-shadow: inset 0 0 0 1px var(--accent);
    color: var(--accent-text);
  }

  .cdot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    flex-shrink: 0;
  }
  .cdur {
    font-size: 8.5px;
    color: var(--text-ghost);
  }
  .chip.on .cdur {
    color: var(--accent-text-dim);
  }
</style>
