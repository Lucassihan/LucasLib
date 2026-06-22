<script lang="ts">
  import { library, removeAuton, summarize, formatRelative } from '$lib/stores/library.svelte';
  import { loadFromLibrary } from '$lib/stores/trace.svelte';

  let { open = $bindable() }: { open: boolean } = $props();

  function close() {
    open = false;
  }

  function onKey(e: KeyboardEvent) {
    if (e.key === 'Escape') close();
  }

  function pick(id: string) {
    loadFromLibrary(id);
    close();
  }

  function fmtDuration(ms: number): string {
    if (ms < 1000) return `${ms} ms`;
    const s = ms / 1000;
    if (s < 10) return `${s.toFixed(2)} s`;
    return `${s.toFixed(1)} s`;
  }

  function fmtAbs(ms: number): string {
    return new Date(ms).toLocaleString(undefined, {
      month: 'short', day: 'numeric',
      hour: '2-digit', minute: '2-digit'
    });
  }
</script>

<svelte:window onkeydown={onKey} />

{#if open}
  <div class="backdrop" onclick={close} role="presentation">
    <div class="panel" onclick={(e) => e.stopPropagation()} role="dialog" aria-label="Library">
      <div class="head">
        <span class="title">Recently imported</span>
        <button class="close" onclick={close} aria-label="Close library">×</button>
      </div>

      <div class="sub">{library.items.length} auton{library.items.length === 1 ? '' : 's'} · click to load</div>

      <ul class="list">
        {#each library.items as a (a.id)}
          {@const stats = summarize(a.text)}
          {@const active = a.id === library.activeId}
          <li class="row" class:active>
            <button class="main" onclick={() => pick(a.id)}>
              <div class="line1">
                <span class="name">{a.name}</span>
                {#if active}<span class="badge">loaded</span>{/if}
              </div>
              <div class="line2">
                <span class="stat">{stats.movements} mvmt{stats.movements === 1 ? '' : 's'}</span>
                <span class="dot">·</span>
                <span class="stat">{fmtDuration(stats.durationMs)}</span>
                <span class="dot">·</span>
                <span class="ts" title={fmtAbs(a.importedAt)}>{formatRelative(a.importedAt)}</span>
              </div>
            </button>
            <button class="del" onclick={() => removeAuton(a.id)} aria-label="Remove {a.name}">×</button>
          </li>
        {/each}
      </ul>

      {#if library.items.length === 0}
        <p class="empty">No autons logged yet. Imported traces will show up here.</p>
      {/if}
    </div>
  </div>
{/if}

<style>
  .backdrop {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.55);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 100;
  }

  .panel {
    width: 420px;
    max-height: 80vh;
    background: var(--bg-elevated);
    border-radius: var(--radius-panel);
    box-shadow:
      var(--ring),
      0 6px 24px rgba(0, 0, 0, 0.4);
    padding: 16px 18px;
    display: flex;
    flex-direction: column;
    min-height: 0;
  }

  .head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 4px;
  }

  .title {
    font-size: 14px;
    font-weight: 700;
    color: var(--text-emphasis);
  }

  .close {
    background: none;
    border: none;
    font-family: inherit;
    font-size: 18px;
    line-height: 1;
    color: var(--text-dim);
    cursor: pointer;
    padding: 0 2px;
    transition: color 0.15s ease;
  }
  .close:hover {
    color: var(--text-primary);
  }

  .sub {
    font-size: 10px;
    font-weight: 500;
    color: var(--text-muted);
    margin-bottom: 12px;
  }

  .list {
    list-style: none;
    margin: 0;
    padding: 0;
    overflow-y: auto;
    flex: 1 1 auto;
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  .row {
    display: flex;
    align-items: stretch;
    gap: 2px;
    border-radius: var(--radius-small);
    background: var(--bg-base);
    box-shadow: var(--ring);
    transition: background 0.15s ease;
  }
  .row:hover {
    background: var(--bg-surface);
  }
  .row.active {
    background: var(--bg-surface);
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.1);
  }

  .main {
    flex: 1;
    background: none;
    border: none;
    text-align: left;
    padding: 9px 12px;
    cursor: pointer;
    color: inherit;
    font-family: inherit;
    display: flex;
    flex-direction: column;
    gap: 3px;
    border-radius: var(--radius-small);
  }

  .line1 {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .name {
    font-size: 12px;
    font-weight: 600;
    color: var(--text-primary);
  }

  .badge {
    font-size: 8px;
    font-weight: 600;
    color: var(--text-muted);
    background: var(--bg-elevated);
    padding: 1px 6px;
    border-radius: 3px;
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }

  .line2 {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 10px;
    color: var(--text-dim);
  }
  .stat {
    color: var(--text-secondary);
  }
  .ts {
    color: var(--text-muted);
    margin-left: auto;
  }
  .dot {
    color: var(--text-ghost);
  }

  .del {
    background: none;
    border: none;
    color: var(--text-ghost);
    font-size: 14px;
    line-height: 1;
    padding: 0 10px;
    cursor: pointer;
    border-radius: 0 var(--radius-small) var(--radius-small) 0;
    transition: color 0.15s ease, background 0.15s ease;
  }
  .del:hover {
    color: var(--text-primary);
    background: var(--bg-raised);
  }

  .empty {
    font-size: 11px;
    color: var(--text-dim);
    text-align: center;
    padding: 20px 0;
  }
</style>
