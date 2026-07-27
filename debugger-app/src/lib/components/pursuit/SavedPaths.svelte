<script lang="ts">
  // File-backed saved-paths drawer (dev-server /api/paths). Save the current
  // path, load / overwrite / delete saved ones, duplicate under a fresh name.
  import { savedPaths, refreshSavedPaths, savePath, duplicatePath, loadPath, deletePath, pursuit } from '$lib/stores/pursuit.svelte';

  let open = $state(false);
  let confirmDel = $state<string | null>(null);

  $effect(() => {
    refreshSavedPaths();
  });

  function ago(ms: number): string {
    const s = (Date.now() - ms) / 1000;
    if (s < 60) return 'just now';
    if (s < 3600) return Math.floor(s / 60) + 'm ago';
    if (s < 86400) return Math.floor(s / 3600) + 'h ago';
    return Math.floor(s / 86400) + 'd ago';
  }

  function onDelete(name: string) {
    if (confirmDel === name) {
      deletePath(name);
      confirmDel = null;
    } else {
      confirmDel = name;
      setTimeout(() => confirmDel === name && (confirmDel = null), 2500);
    }
  }
</script>

<section class="panel group">
  <div class="head-row">
    <button class="disc" onclick={() => (open = !open)} aria-expanded={open}>
      <span class="chev" class:open>▸</span>
      <span class="gtitle">Saved paths</span>
      {#if savedPaths.length}<span class="count">{savedPaths.length}</span>{/if}
    </button>
    <div class="acts">
      <button class="mini" onclick={() => savePath()} title="Save the current path">Save</button>
      <button class="mini" onclick={duplicatePath} title="Save a copy under a new name">Duplicate</button>
    </div>
  </div>

  {#if open}
    {#if savedPaths.length === 0}
      <div class="empty">No saved paths yet — hit Save.</div>
    {:else}
      <div class="list">
        {#each savedPaths as p (p.name)}
          <div class="row" class:active={p.name === pursuit.name}>
            <button class="load" onclick={() => loadPath(p)} title="Load this path">
              <span class="nm">{p.name}</span>
              <span class="when">{ago(p.savedAt)}</span>
            </button>
            <button class="ic" onclick={() => savePath(p.name)} title="Overwrite with the current path" aria-label="Overwrite">↻</button>
            <button class="ic del" class:armed={confirmDel === p.name} onclick={() => onDelete(p.name)} title="Delete" aria-label="Delete">
              {confirmDel === p.name ? '✓' : '×'}
            </button>
          </div>
        {/each}
      </div>
    {/if}
  {/if}
</section>

<style>
  .group {
    padding: 10px 12px;
    display: flex;
    flex-direction: column;
    gap: 8px;
    flex-shrink: 0;
  }
  .head-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
  }
  .disc {
    display: flex;
    align-items: center;
    gap: 6px;
    background: none;
    border: none;
    padding: 0;
    cursor: pointer;
  }
  .chev {
    font-size: 9px;
    color: var(--text-ghost);
    transition: transform 0.15s ease;
  }
  .chev.open {
    transform: rotate(90deg);
  }
  .gtitle {
    font-size: 11px;
    font-weight: 700;
    color: var(--text-primary);
  }
  .count {
    font-size: 8.5px;
    font-weight: 700;
    color: var(--bg-base);
    background: var(--text-ghost);
    border-radius: 8px;
    padding: 1px 6px;
  }
  .acts {
    display: flex;
    gap: 5px;
  }
  .mini {
    padding: 4px 10px;
    border: none;
    border-radius: var(--radius-small);
    background: var(--bg-base);
    box-shadow: var(--ring);
    color: var(--text-secondary);
    font-family: inherit;
    font-size: 9.5px;
    font-weight: 600;
    cursor: pointer;
    transition: color 0.12s ease, background 0.12s ease;
  }
  .mini:hover {
    color: #fbbf24;
    background: var(--bg-raised);
  }
  .empty {
    font-size: 9px;
    color: var(--text-ghost);
    padding: 2px 0 4px;
  }
  .list {
    display: flex;
    flex-direction: column;
    gap: 3px;
    max-height: 160px;
    overflow-y: auto;
  }
  .row {
    display: flex;
    align-items: stretch;
    gap: 3px;
  }
  .row.active .nm {
    color: #fbbf24;
  }
  .load {
    flex: 1;
    min-width: 0;
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 8px;
    padding: 6px 9px;
    border: none;
    border-radius: var(--radius-small);
    background: var(--bg-base);
    box-shadow: var(--ring);
    cursor: pointer;
    text-align: left;
    transition: background 0.12s ease;
  }
  .load:hover {
    background: var(--bg-raised);
  }
  .nm {
    font-size: 11px;
    font-weight: 600;
    color: var(--text-primary);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .when {
    font-size: 8px;
    color: var(--text-ghost);
    flex-shrink: 0;
    font-variant-numeric: tabular-nums;
  }
  .ic {
    width: 24px;
    flex-shrink: 0;
    border: none;
    border-radius: var(--radius-small);
    background: var(--bg-base);
    box-shadow: var(--ring);
    color: var(--text-muted);
    font-size: 13px;
    line-height: 1;
    cursor: pointer;
    transition: color 0.12s ease, background 0.12s ease;
  }
  .ic:hover {
    color: var(--text-primary);
    background: var(--bg-raised);
  }
  .ic.del:hover {
    color: #f87171;
  }
  .ic.del.armed {
    color: var(--bg-base);
    background: #f87171;
  }
</style>
