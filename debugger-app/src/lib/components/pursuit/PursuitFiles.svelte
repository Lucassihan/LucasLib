<script lang="ts">
  // Path tabs + the saved-path drawer — SandboxCode.svelte's file system,
  // ported onto pursuit's document model. Same rules there and here: tabs each
  // hold their own path, + opens the folder listing (open-only), Save writes
  // back to the open file and falls through to Save as when untitled, and the
  // two destructive actions arm on the first click instead of calling
  // confirm() (unavailable in the Tauri webview).
  import {
    pursuit,
    newPath,
    saveCurrentPath,
    savePathAs,
    loadPath,
    deletePath,
    refreshSavedPaths,
    switchTab,
    closeTab,
    isTabDirty
  } from '$lib/stores/pursuit.svelte';

  let pickerOpen = $state(false);
  let savingAs = $state(false);
  let saveAsName = $state('');
  let saveAsInput: HTMLInputElement | undefined = $state();

  $effect(() => {
    refreshSavedPaths();
  });

  function togglePicker() {
    pickerOpen = !pickerOpen;
    if (pickerOpen) refreshSavedPaths();
    savingAs = false;
  }

  let armedClose = $state<string | null>(null); // tab id, not index — indices shift on close
  let armedDelete = $state<string | null>(null); // file name
  let armTimer: ReturnType<typeof setTimeout> | null = null;

  /** Arm one action and disarm everything else, auto-expiring so a stray click
   *  never leaves a live trigger sitting under the cursor. */
  function arm(which: 'close' | 'delete', key: string) {
    armedClose = which === 'close' ? key : null;
    armedDelete = which === 'delete' ? key : null;
    if (armTimer) clearTimeout(armTimer);
    armTimer = setTimeout(disarm, 3500);
  }
  function disarm() {
    armedClose = null;
    armedDelete = null;
    if (armTimer) clearTimeout(armTimer);
    armTimer = null;
  }
  $effect(() => () => {
    if (armTimer) clearTimeout(armTimer);
  });

  function onCloseTab(i: number) {
    const tab = pursuit.tabs[i];
    if (!tab) return;
    // A clean tab has nothing to lose, so it closes on the first click.
    if (isTabDirty(i) && armedClose !== tab.id) {
      arm('close', tab.id);
      return;
    }
    disarm();
    closeTab(i);
  }

  function onDeleteFile(name: string) {
    if (armedDelete !== name) {
      arm('delete', name);
      return;
    }
    disarm();
    deletePath(name);
  }

  function onNewTab() {
    newPath();
    pickerOpen = false;
  }

  function onPickFile(name: string) {
    loadPath(name);
    pickerOpen = false;
  }

  /** Save — falls through to Save as when there's no file to overwrite yet
   *  (an untitled tab). Shared by the Save button and ⌘S. */
  async function onSave() {
    if (!pursuit.dirty) return;
    if (!pursuit.openPath) {
      beginSaveAs();
      return;
    }
    await saveCurrentPath();
  }

  function beginSaveAs() {
    saveAsName = pursuit.openPath ?? '';
    savingAs = true;
    pickerOpen = false;
    queueMicrotask(() => {
      saveAsInput?.focus();
      saveAsInput?.select();
    });
  }

  function cancelSaveAs() {
    savingAs = false;
  }

  async function commitSaveAs() {
    if (!saveAsName.trim()) return;
    await savePathAs(saveAsName);
    savingAs = false;
  }

  // ⌘S saves. ⌘Z/⌘⇧Z/⌘Y are PursuitField's — it owns the canvas shortcuts and
  // already routes them into the same per-tab history.
  function onGlobalKeydown(e: KeyboardEvent) {
    if (!(e.metaKey || e.ctrlKey) || e.key.toLowerCase() !== 's') return;
    const t = e.target instanceof HTMLElement ? e.target : null;
    if (t && t.closest('input, textarea')) return; // typing a file name
    e.preventDefault();
    onSave();
  }

  function fmtDate(ms: number): string {
    return new Date(ms).toLocaleDateString(undefined, { month: 'short', day: 'numeric' });
  }
</script>

<svelte:window onkeydown={onGlobalKeydown} />

<section class="panel filespanel">
  <!-- tab strip — active tab lifted with a gold top-edge accent, an unsaved dot
       after the name, then ×, then +, then Save at the right (disabled when the
       active tab is clean). -->
  <div class="tabstrip">
    {#each pursuit.tabs as tab, i (tab.id)}
      <div class="tab" class:active={i === pursuit.activeTab}>
        <button type="button" class="tab-body" onclick={() => switchTab(i)} title={tab.name ?? 'Untitled'}>
          <span class="tab-name">{tab.name ?? 'Untitled'}</span>
          {#if isTabDirty(i)}<span class="tab-dot" aria-hidden="true"></span>{/if}
        </button>
        <button
          type="button"
          class="tab-close"
          class:armed={armedClose === tab.id}
          onclick={() => onCloseTab(i)}
          title={armedClose === tab.id ? 'Click again to discard unsaved changes' : 'Close'}
          aria-label="Close tab"
        >{armedClose === tab.id ? 'Discard?' : '×'}</button>
      </div>
    {/each}
    <button type="button" class="tab-add" class:on={pickerOpen} onclick={togglePicker} title="Open a saved path" aria-label="Open">
      +
    </button>
    <div class="tabstrip-spacer"></div>
    <button type="button" class="tab-save" disabled={!pursuit.dirty} onclick={onSave} title="Save (⌘S)">Save</button>
  </div>

  {#if pickerOpen}
    <div class="savedbar">
      <button class="savedload new" onclick={onNewTab}>
        <span class="sname">+ New untitled</span>
      </button>
      {#if pursuit.saved.length === 0}
        <div class="saved-empty">No paths in the folder yet.</div>
      {:else}
        <div class="savedlist">
          {#each pursuit.saved as p (p.name)}
            <div class="savedrow">
              <button class="savedload" onclick={() => onPickFile(p.name)} title="Open into a tab (focuses it if already open)">
                <span class="sname">{p.name}</span>
                <span class="sdate">{fmtDate(p.savedAt)}</span>
              </button>
              <button
                class="sdel"
                class:armed={armedDelete === p.name}
                onclick={() => onDeleteFile(p.name)}
                title={armedDelete === p.name ? 'Click again to delete this file from the folder' : 'Delete the file'}
              >{armedDelete === p.name ? 'Delete?' : '×'}</button>
            </div>
          {/each}
        </div>
      {/if}
    </div>
  {/if}

  <!-- Save as — the only place a name is ever typed. Shown for an untitled
       tab's first Save, or explicitly via beginSaveAs. -->
  {#if savingAs}
    <form
      class="saverow"
      onsubmit={(e) => {
        e.preventDefault();
        commitSaveAs();
      }}
    >
      <input bind:this={saveAsInput} class="savename" placeholder="save as…" bind:value={saveAsName} />
      <button class="savebtn" type="submit" disabled={!saveAsName.trim()}>save</button>
      <button class="savebtn cancel" type="button" onclick={cancelSaveAs}>cancel</button>
    </form>
  {/if}
</section>

<style>
  .filespanel {
    display: flex;
    flex-direction: column;
    gap: 8px;
    padding: 10px 12px;
    flex-shrink: 0;
  }
  .tabstrip {
    display: flex;
    align-items: stretch;
    gap: 3px;
    flex-shrink: 0;
    overflow-x: auto;
  }
  .tab {
    display: flex;
    align-items: center;
    flex-shrink: 0;
    max-width: 160px;
    min-width: 0;
    background: var(--bg-base);
    border-radius: var(--radius-small) var(--radius-small) 0 0;
    box-shadow: var(--ring);
    color: var(--text-ghost);
    transition: color 0.15s ease, background 0.15s ease;
  }
  .tab:hover {
    color: var(--text-secondary);
  }
  /* active tab: lifted (panel-surface background) + a gold top-edge accent */
  .tab.active {
    background: var(--bg-surface);
    color: var(--text-primary);
    box-shadow:
      var(--ring),
      inset 0 2px 0 0 #ecd88f;
  }
  .tab-body {
    display: flex;
    align-items: center;
    gap: 5px;
    min-width: 0;
    flex: 1;
    background: none;
    border: none;
    color: inherit;
    font-family: inherit;
    font-size: 10px;
    font-weight: 600;
    padding: 6px 2px 6px 10px;
    cursor: pointer;
  }
  .tab-name {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .tab-dot {
    width: 5px;
    height: 5px;
    border-radius: 50%;
    background: #ecd88f;
    flex-shrink: 0;
  }
  .tab-close {
    flex-shrink: 0;
    background: none;
    border: none;
    color: var(--text-ghost);
    font-size: 11px;
    line-height: 1;
    padding: 4px 8px;
    cursor: pointer;
    transition: color 0.15s ease;
  }
  .tab-close:hover {
    color: #f87171;
  }
  /* armed = one more click discards. Reads as a warning rather than a glyph,
     because the whole point is that it is no longer the same action. */
  .tab-close.armed {
    color: #f87171;
    font-size: 9.5px;
    font-weight: 600;
    padding: 3px 6px;
    border-radius: 4px;
    background: rgba(248, 113, 113, 0.14);
  }
  .tab-add {
    flex-shrink: 0;
    background: none;
    border: none;
    color: var(--text-ghost);
    font-size: 13px;
    font-weight: 600;
    padding: 0 9px;
    cursor: pointer;
    border-radius: var(--radius-small);
    transition: color 0.15s ease, background 0.15s ease;
  }
  .tab-add:hover,
  .tab-add.on {
    color: var(--text-primary);
    background: var(--bg-base);
  }
  .tabstrip-spacer {
    flex: 1;
    min-width: 8px;
  }
  .tab-save {
    flex-shrink: 0;
    align-self: center;
    background: var(--bg-raised);
    border: none;
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    color: var(--text-secondary);
    font-family: inherit;
    font-size: 9.5px;
    font-weight: 700;
    padding: 5px 12px;
    cursor: pointer;
    transition: color 0.15s ease;
  }
  .tab-save:hover:not(:disabled) {
    color: var(--text-primary);
  }
  .tab-save:disabled {
    opacity: 0.4;
    cursor: default;
  }

  /* saved-paths picker (open-only) + save-as popover */
  .savedbar {
    display: flex;
    flex-direction: column;
    gap: 6px;
    background: var(--bg-base);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 8px 10px;
    max-height: 150px;
    overflow-y: auto;
    flex-shrink: 0;
  }
  .saverow {
    display: flex;
    gap: 6px;
    flex-shrink: 0;
  }
  .savename {
    flex: 1;
    background: var(--bg-surface);
    border: none;
    border-radius: 4px;
    box-shadow: var(--ring);
    color: var(--text-primary);
    font-family: inherit;
    font-size: 10px;
    padding: 4px 8px;
    outline: none;
  }
  .savename:focus {
    box-shadow:
      var(--ring),
      0 0 0 1px rgba(255, 255, 255, 0.14);
  }
  .savebtn {
    background: var(--bg-raised);
    border: none;
    border-radius: 4px;
    box-shadow: var(--ring);
    color: var(--text-secondary);
    font-family: inherit;
    font-size: 9.5px;
    font-weight: 600;
    padding: 4px 10px;
    cursor: pointer;
    transition: color 0.15s ease;
  }
  .savebtn:hover:not(:disabled) {
    color: var(--text-primary);
  }
  .savebtn:disabled {
    opacity: 0.4;
    cursor: default;
  }
  .savebtn.cancel {
    color: var(--text-ghost);
  }
  .saved-empty {
    font-size: 9px;
    color: var(--text-ghost);
  }
  .savedlist {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  .savedrow {
    display: flex;
    align-items: center;
    gap: 4px;
  }
  .savedload {
    flex: 1;
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: 8px;
    background: none;
    border: none;
    border-radius: 4px;
    padding: 3px 6px;
    cursor: pointer;
    font-family: inherit;
    transition: background 0.15s ease;
  }
  .savedload:hover {
    background: var(--bg-surface);
  }
  /* "+ New untitled" row — its own gold to read as a distinct action from
     the file rows below it */
  .savedload.new .sname {
    color: #ecd88f;
  }
  .sname {
    font-size: 10px;
    font-weight: 600;
    color: var(--text-secondary);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .savedload:hover .sname {
    color: var(--text-primary);
  }
  .sdate {
    font-size: 8.5px;
    color: var(--text-ghost);
    flex-shrink: 0;
  }
  .sdel {
    background: none;
    border: none;
    color: var(--text-ghost);
    font-size: 11px;
    line-height: 1;
    padding: 2px 4px;
    cursor: pointer;
    border-radius: 3px;
    transition: color 0.15s ease;
  }
  .sdel:hover {
    color: #f87171;
  }
  .sdel.armed {
    color: #f87171;
    font-size: 9.5px;
    font-weight: 600;
    padding: 2px 6px;
    background: rgba(248, 113, 113, 0.14);
  }
</style>
