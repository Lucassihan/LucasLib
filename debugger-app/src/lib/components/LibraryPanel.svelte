<script lang="ts">
  import { library, removeAuton, summarize, formatRelative } from '$lib/stores/library.svelte';
  import { loadFromLibrary, loadTrace, traceState, setGhost, clearGhost } from '$lib/stores/trace.svelte';
  import { invoke, Channel } from '@tauri-apps/api/core';

  let { open = $bindable() }: { open: boolean } = $props();

  let importText = $state('');
  let importError = $state('');

  // Live USB capture is only available in the desktop (Tauri) build.
  const isTauri = typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window;
  let capturing = $state(false);
  let captureMsg = $state('');

  async function captureFromRobot() {
    if (!isTauri || capturing) return;
    capturing = true;
    captureMsg = 'Scanning for the V5…';
    try {
      const onStatus = new Channel<string>();
      onStatus.onmessage = (m) => (captureMsg = m);
      const text = await invoke<string>('capture_trace', { timeoutSecs: 90, onStatus });
      loadTrace(text);
      capturing = false;
      captureMsg = '';
      close();
    } catch (e) {
      captureMsg = String(e);
      capturing = false;
    }
  }

  function close() {
    open = false;
  }

  function doImport() {
    const text = importText.trim();
    if (!text) return;
    try {
      loadTrace(text);
      importText = '';
      importError = '';
      close();
    } catch {
      importError = 'Could not parse that trace.';
    }
  }

  async function pasteImport() {
    try {
      importText = await navigator.clipboard.readText();
    } catch {
      importError = 'Clipboard blocked — paste into the box instead.';
    }
  }

  function onKey(e: KeyboardEvent) {
    if (e.key === 'Escape') close();
  }

  function pick(id: string) {
    loadFromLibrary(id);
    close();
  }

  // Pin a run as the comparison ghost (faint overlay on the field).
  function toggleGhost(id: string) {
    if (traceState.ghost?.id === id) clearGhost();
    else setGhost(id);
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

      <div class="import">
        {#if isTauri}
          <button class="capture" onclick={captureFromRobot} disabled={capturing}>
            {capturing ? 'Capturing…' : '⚡ Capture from robot'}
          </button>
          {#if captureMsg}<span class="capture-msg">{captureMsg}</span>{/if}
          <div class="or">or paste a dump</div>
        {/if}
        <textarea
          class="import-box"
          placeholder="Paste a robot trace dump from the console (debugger::stop output)…"
          bind:value={importText}
          spellcheck="false"
        ></textarea>
        <div class="import-actions">
          {#if importError}<span class="import-err">{importError}</span>{/if}
          <button class="ibtn" onclick={pasteImport}>Paste</button>
          <button class="ibtn primary" onclick={doImport} disabled={!importText.trim()}>Import</button>
        </div>
      </div>

      <ul class="list">
        {#each library.items as a (a.id)}
          {@const stats = summarize(a.text)}
          {@const active = a.id === library.activeId}
          <li class="row" class:active>
            <button class="main" onclick={() => pick(a.id)}>
              <div class="line1">
                <span class="name">{a.name}</span>
                {#if active}<span class="badge">loaded</span>{/if}
                {#if traceState.ghost?.id === a.id}<span class="badge ghost">ghost</span>{/if}
              </div>
              <div class="line2">
                <span class="stat">{stats.movements} mvmt{stats.movements === 1 ? '' : 's'}</span>
                <span class="dot">·</span>
                <span class="stat">{fmtDuration(stats.durationMs)}</span>
                <span class="dot">·</span>
                <span class="ts" title={fmtAbs(a.importedAt)}>{formatRelative(a.importedAt)}</span>
              </div>
            </button>
            <button
              class="ghostbtn"
              class:on={traceState.ghost?.id === a.id}
              onclick={() => toggleGhost(a.id)}
              aria-label="Pin {a.name} as comparison ghost"
              title="Overlay this run on the field as a ghost"
            >
              <svg viewBox="0 0 12 12" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linejoin="round">
                <path d="M6 1.2 C3.5 1.2 2 3.1 2 5.4 V10.6 L3.35 9.4 L4.7 10.6 L6 9.4 L7.3 10.6 L8.65 9.4 L10 10.6 V5.4 C10 3.1 8.5 1.2 6 1.2 Z" />
              </svg>
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

  /* import box */
  .import {
    display: flex;
    flex-direction: column;
    gap: 6px;
    margin-bottom: 12px;
    padding-bottom: 12px;
    border-bottom: 1px solid var(--border-subtle);
  }
  .capture {
    width: 100%;
    background: #2563eb;
    color: #fff;
    border: none;
    border-radius: var(--radius-small);
    padding: 9px 12px;
    font-family: inherit;
    font-size: 12px;
    font-weight: 700;
    cursor: pointer;
    transition: background 0.15s ease;
  }
  .capture:hover {
    background: #1d4ed8;
  }
  .capture:disabled {
    opacity: 0.6;
    cursor: default;
  }
  .capture-msg {
    font-size: 9px;
    color: var(--text-secondary);
    line-height: 1.4;
  }
  .or {
    font-size: 9px;
    color: var(--text-ghost);
    text-align: center;
    margin: 1px 0 -2px;
  }

  .import-box {
    width: 100%;
    box-sizing: border-box;
    height: 46px;
    resize: vertical;
    background: var(--bg-base);
    color: var(--text-secondary);
    border: none;
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 7px 9px;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 10px;
    line-height: 1.4;
  }
  .import-box:focus {
    outline: none;
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.18);
  }
  .import-actions {
    display: flex;
    align-items: center;
    gap: 8px;
    justify-content: flex-end;
  }
  .import-err {
    margin-right: auto;
    font-size: 9px;
    color: #f87171;
  }
  .ibtn {
    background: var(--bg-raised);
    color: var(--text-secondary);
    border: none;
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 5px 12px;
    font-family: inherit;
    font-size: 11px;
    font-weight: 600;
    cursor: pointer;
    transition: background 0.15s ease, color 0.15s ease;
  }
  .ibtn:hover {
    color: var(--text-primary);
  }
  .ibtn.primary {
    background: #2563eb;
    color: #fff;
  }
  .ibtn:disabled {
    opacity: 0.4;
    cursor: default;
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

  .badge.ghost {
    color: #b0b0c8;
    box-shadow: inset 0 0 0 1px rgba(176, 176, 200, 0.35);
  }

  .ghostbtn {
    display: flex;
    align-items: center;
    background: none;
    border: none;
    color: var(--text-ghost);
    padding: 0 6px;
    cursor: pointer;
    transition: color 0.15s ease, background 0.15s ease;
  }
  .ghostbtn svg {
    width: 12px;
    height: 12px;
  }
  .ghostbtn:hover {
    color: var(--text-primary);
    background: var(--bg-raised);
  }
  .ghostbtn.on {
    color: #b0b0c8;
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
