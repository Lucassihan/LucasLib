<script lang="ts">
  import {
    sandbox,
    getRoute,
    newAuton,
    saveCurrentAuton,
    saveAutonAs,
    loadAuton,
    deleteAuton,
    refreshSavedAutons,
    resetSandboxCode,
    switchTab,
    closeTab,
    isTabDirty,
    pushUndo,
    undo,
    redo
  } from '$lib/stores/sandbox.svelte';
  import { FN_BY_NAME, STRUCT_FIELDS, STRUCT_NAMES } from '$lib/sandbox/spec';
  import SandboxEditor from './SandboxEditor.svelte';

  const route = $derived(getRoute());
  const errors = $derived(route.parsed.errors);

  let caret = $state(0);
  let editorRef: SandboxEditor | undefined = $state();

  // --- editor tabs (Feature 1) -------------------------------------------------
  // Lucas picked tabs over a dropdown specifically because his saved files are
  // versions of one route (sawp v1 … v4) and he flips between them — each tab
  // holds its own buffer (sandbox.svelte.ts's TabState), so switching tabs
  // never loses anything. Only CLOSING a dirty tab can lose work, hence the
  // confirm in onCloseTab below; opening/newing only ever adds or focuses a
  // tab, so neither one needs to ask first.
  let pickerOpen = $state(false); // the repurposed drawer — open-only now, no save
  let savingAs = $state(false);
  let saveAsName = $state('');
  let saveAsInput: HTMLInputElement | undefined = $state();

  // load the folder listing once on mount, and again whenever the picker opens
  $effect(() => {
    refreshSavedAutons();
  });

  function togglePicker() {
    pickerOpen = !pickerOpen;
    if (pickerOpen) refreshSavedAutons();
    savingAs = false;
  }

  // caret is component-local (it drives the caret-tracking effect below,
  // which re-derives sandbox.selected from wherever it points) — a stale
  // offset left over from the PREVIOUS tab's text could coincidentally land
  // inside some unrelated statement in the new one, so every tab-changing
  // action here resets it to 0 alongside the store's own transient-state reset.
  function onSwitchTab(i: number) {
    if (i === sandbox.activeTab) return;
    switchTab(i);
    caret = 0;
  }

  // Destructive actions arm on the first click and fire on the second, rather
  // than going through window.confirm(). The desktop build runs in a Tauri
  // webview where the native dialogs are not available — confirm() there
  // returns false, so the old code silently refused to close a dirty tab and
  // looked broken. This also matches the two-click reset confirm the
  // Trajectory Generator already uses, and it gives the file delete a
  // confirmation it never had.
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
    const tab = sandbox.tabs[i];
    if (!tab) return;
    // A clean tab has nothing to lose, so it closes on the first click.
    if (isTabDirty(i) && armedClose !== tab.id) {
      arm('close', tab.id);
      return;
    }
    disarm();
    const wasActive = i === sandbox.activeTab;
    closeTab(i);
    if (wasActive) caret = 0;
  }

  function onDeleteFile(name: string) {
    if (armedDelete !== name) {
      arm('delete', name);
      return;
    }
    disarm();
    deleteAuton(name);
  }

  function onNewTab() {
    newAuton();
    caret = 0;
    pickerOpen = false;
  }

  function onPickFile(name: string) {
    loadAuton(name);
    caret = 0;
    pickerOpen = false;
  }

  function onReset() {
    resetSandboxCode();
    caret = 0;
  }

  /** Save — falls through to Save as when there's no file to overwrite yet
   *  (an untitled tab). Shared by the Save button and ⌘S. */
  async function onSave() {
    if (!sandbox.dirty) return;
    if (!sandbox.openAuton) {
      beginSaveAs();
      return;
    }
    await saveCurrentAuton();
  }

  function beginSaveAs() {
    saveAsName = sandbox.openAuton ?? '';
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
    const name = saveAsName.trim();
    if (!name) return;
    await saveAutonAs(name);
    savingAs = false;
  }

  // --- ⌘Z over the route text (Feature 3) --------------------------------------
  // SandboxEditor is a controlled contenteditable that rebuilds its own DOM on
  // every edit, which destroys the browser's native undo — this is the
  // explicit replacement, mirroring pursuit.svelte.ts's pushHistory/undo/redo.

  /** Every keystroke pushes before the value changes, coalesced under one key
   *  so a burst of typing is one undo step rather than one per character
   *  (sandbox.svelte.ts's pushUndo, 700ms window — same shape as pursuit). */
  function onEditorInput(v: string) {
    pushUndo('type');
    sandbox.code = v;
  }

  function onUndo() {
    if (!sandbox.tabs[sandbox.activeTab]?.undo.length) return;
    undo();
    // Caret placement after an external (non-typed) change to `value` is
    // SandboxEditor's syncExternalChange — see its own doc for why this lands
    // at end-of-text rather than the exact pre-edit offset.
    editorRef?.syncExternalChange(sandbox.code.length);
  }

  function onRedo() {
    if (!sandbox.tabs[sandbox.activeTab]?.redo.length) return;
    redo();
    editorRef?.syncExternalChange(sandbox.code.length);
  }

  // ⌘S saves, ⌘Z/⌘⇧Z/⌘Y undo/redo — bound globally rather than gated on focus
  // like +page.svelte's playback shortcuts, because the whole point of both
  // features is that they work WHILE typing in the code editor. The only
  // thing that blocks them is focus sitting in a plain text <input> (the
  // Save-as name field, the sole one in this panel) — typing a file name
  // keeps the browser's own Cmd+Z/Cmd+S text-field behavior instead of it
  // being hijacked into the route's history or a network request.
  function onGlobalKeydown(e: KeyboardEvent) {
    if (!(e.metaKey || e.ctrlKey)) return;
    const t = e.target instanceof HTMLElement ? e.target : null;
    const inNameField = !!(t && t.closest('input, textarea'));
    const key = e.key.toLowerCase();
    if (key === 's') {
      if (inNameField) return;
      e.preventDefault();
      onSave();
    } else if (key === 'z') {
      if (inNameField) return;
      e.preventDefault();
      if (e.shiftKey) onRedo();
      else onUndo();
    } else if (key === 'y') {
      if (inNameField) return;
      e.preventDefault();
      onRedo();
    }
  }

  function fmtDate(ms: number): string {
    return new Date(ms).toLocaleDateString(undefined, { month: 'short', day: 'numeric' });
  }

  // Track which statement the caret sits in → drives selection on the field
  // (and, for a trajectory call or the pathDecl it references, which
  // control-point handles show and whether the path panel replaces the
  // regular params column). Clears on the way out — a caret sitting between
  // statements, in a comment, or on a blank line selects nothing.
  $effect(() => {
    const c = caret;
    const stmts = route.parsed.stmts;
    for (let i = 0; i < stmts.length; i++) {
      if (c >= stmts[i].start && c <= stmts[i].end) {
        sandbox.selected = i;
        return;
      }
    }
    sandbox.selected = null;
  });

  // --- signature hint: scan text around the caret (works mid-typing) ---
  interface Hint {
    fn: string;
    argIndex: number;
    inBraces: boolean;
    structKind?: string;
  }

  const hint = $derived.by((): Hint | null => {
    const src = sandbox.code;
    const c = Math.min(caret, src.length);
    // BoomParams declaration initializer first — the paren scan below bails at
    // the previous statement's ';' and would never reach this
    const stmtStart = src.lastIndexOf(';', c - 1) + 1;
    if (/BoomParams\s+\w+\s*=?\s*\{[^}]*$/.test(src.slice(stmtStart, c))) {
      return { fn: 'BoomParams', argIndex: 0, inBraces: true, structKind: 'boomParams' };
    }
    // walk backward: find the innermost unmatched '(' before the caret,
    // counting top-level commas and whether we're inside a brace initializer
    let depthPar = 0;
    let depthBrace = 0;
    let commas = 0;
    let inBraces = false;
    for (let i = c - 1; i >= 0; i--) {
      const ch = src[i];
      if (ch === ';') return null;
      if (ch === ')') depthPar++;
      else if (ch === '}') depthBrace++;
      else if (ch === '{') {
        if (depthBrace > 0) depthBrace--;
        else {
          // caret is inside this initializer — commas counted so far were
          // FIELD separators, not argument separators
          inBraces = true;
          commas = 0;
        }
      } else if (ch === '(') {
        if (depthPar > 0) depthPar--;
        else {
          // found the call's paren — read the identifier before it
          let j = i - 1;
          while (j >= 0 && /\s/.test(src[j])) j--;
          let k = j;
          while (k >= 0 && /[A-Za-z0-9_.]/.test(src[k])) k--;
          const name = src.slice(k + 1, j + 1);
          if (!name) return null;
          const fn = FN_BY_NAME[name];
          if (!fn) return null;
          const argIndex = Math.min(commas, fn.params.length - 1);
          const kind = fn.params[argIndex]?.kind;
          const structKind =
            inBraces && (kind === 'lateralEc' || kind === 'turnEc' || kind === 'blend' || kind === 'boomParams')
              ? kind
              : undefined;
          return { fn: name, argIndex, inBraces, structKind };
        }
      } else if (ch === ',' && depthPar === 0 && depthBrace === 0) {
        commas++;
      }
    }
    return null;
  });

  const hintFn = $derived(hint && hint.fn !== 'BoomParams' ? FN_BY_NAME[hint.fn] : null);
  const hintFields = $derived(hint?.structKind ? STRUCT_FIELDS[hint.structKind] : null);

  // the sandbox-configured default for an ec field (falls back to the lib default)
  function fieldDefault(structKind: string | undefined, f: { name: string; def: number | boolean }): number | boolean {
    const k = sandbox.knobs;
    if (structKind === 'lateralEc') {
      if (f.name === 't') return k.ecLatT;
      if (f.name === 'v') return k.ecLatV;
      if (f.name === 'd') return k.ecLatD;
      if (f.name === 'settleWithin') return k.ecLatWithin;
    } else if (structKind === 'turnEc') {
      if (f.name === 't') return k.ecTurnT;
      if (f.name === 'v') return k.ecTurnV;
      if (f.name === 'd') return k.ecTurnD;
      if (f.name === 'settleWithin') return k.ecTurnWithin;
    }
    return f.def;
  }

  // line of the field-selected movement → highlighted in the editor
  const selLine = $derived.by(() => {
    const i = sandbox.selected;
    if (i === null) return null;
    const s = route.parsed.stmts[i];
    return s ? s.line : null;
  });
</script>

<svelte:window onkeydown={onGlobalKeydown} />

<section class="panel codepanel">
  <!-- tab strip (Feature 1) — active tab lifted with a gold top-edge accent,
       an unsaved dot after the name, then ×, then +, then Save at the right
       (disabled when the active tab is clean). -->
  <div class="tabstrip">
    {#each sandbox.tabs as tab, i (tab.id)}
      <div class="tab" class:active={i === sandbox.activeTab}>
        <button type="button" class="tab-body" onclick={() => onSwitchTab(i)} title={tab.name ?? 'Untitled'}>
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
    <button type="button" class="tab-add" class:on={pickerOpen} onclick={togglePicker} title="Open a saved auton" aria-label="Open">
      +
    </button>
    <div class="tabstrip-spacer"></div>
    <button type="button" class="tab-save" disabled={!sandbox.dirty} onclick={onSave} title="Save (⌘S)">Save</button>
  </div>

  <div class="head">
    <span class="lbl">Route code</span>
    <span class="sub">LucasLib syntax · paste-ready</span>
    <button class="reset" onclick={onReset} title="Replace the active tab's code with the starter route">reset</button>
  </div>

  <!-- + opens this as an OPEN-ONLY list — no save affordance lives here any
       more (Save/Save as are the tab strip's own Save button + the popover
       below). -->
  {#if pickerOpen}
    <div class="savedbar">
      <button class="savedload new" onclick={onNewTab}>
        <span class="sname">+ New untitled</span>
      </button>
      {#if sandbox.saved.length === 0}
        <div class="saved-empty">No autons in the folder yet.</div>
      {:else}
        <div class="savedlist">
          {#each sandbox.saved as a (a.name)}
            <div class="savedrow">
              <button class="savedload" onclick={() => onPickFile(a.name)} title="Open into a tab (focuses it if already open)">
                <span class="sname">{a.name}</span>
                <span class="sdate">{fmtDate(a.savedAt)}</span>
              </button>
              <button
                class="sdel"
                class:armed={armedDelete === a.name}
                onclick={() => onDeleteFile(a.name)}
                title={armedDelete === a.name ? 'Click again to delete this file from the folder' : 'Delete the file'}
              >{armedDelete === a.name ? 'Delete?' : '×'}</button>
            </div>
          {/each}
        </div>
      {/if}
    </div>
  {/if}

  <!-- Save as — the only place a name is ever typed (Feature 2). Shown for an
       untitled tab's first Save, or explicitly via beginSaveAs. -->
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

  <SandboxEditor
    bind:this={editorRef}
    value={sandbox.code}
    parsed={route.parsed}
    oninput={onEditorInput}
    oncaret={(c) => (caret = c)}
    highlightLine={selLine}
  />

  <!-- signature hint for the call under the caret -->
  <div class="hintbox">
    {#if hintFields && hint}
      <div class="sig">
        <span class="sig-name">{hint.fn === 'BoomParams' ? 'BoomParams' : STRUCT_NAMES[hint.structKind!]}</span>
        <span class="sig-rest">{'{'} designated initializers {'}'}</span>
      </div>
      <div class="fields">
        {#each hintFields as f}
          <span class="field"><code>.{f.name}</code> <span class="fdoc">{f.doc} · default {String(fieldDefault(hint.structKind, f))}</span></span>
        {/each}
      </div>
    {:else if hintFn && hint}
      <div class="sig">
        {#each hintFn.params as p, i}
          {#if i === 0}<span class="sig-name">{hintFn.name}(</span>{/if}
          <span class="sig-arg" class:active={i === hint.argIndex}
            >{p.name}{p.def !== undefined ? ` = ${p.def}` : ''}</span
          >{i < hintFn.params.length - 1 ? ', ' : ''}
          {#if i === hintFn.params.length - 1}<span class="sig-name">)</span>{/if}
        {/each}
      </div>
      <div class="doc">{hintFn.params[hint.argIndex]?.doc ?? hintFn.doc}</div>
    {:else}
      <div class="doc quiet">Type a movement — signatures appear here. Field drags rewrite the text.</div>
    {/if}
  </div>

  {#if errors.length}
    <div class="errors">
      {#each errors.slice(0, 5) as e}
        <div class="err"><span class="eline">L{e.line}</span> {e.message}</div>
      {/each}
      {#if errors.length > 5}<div class="err more">+{errors.length - 5} more…</div>{/if}
    </div>
  {/if}
</section>

<style>
  .codepanel {
    flex: 1;
    min-height: 0;
    display: flex;
    flex-direction: column;
    padding: 12px 14px;
    gap: 8px;
  }

  /* --- tab strip (Feature 1) --- */
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

  .head {
    display: flex;
    align-items: baseline;
    gap: 8px;
  }
  .lbl {
    font-size: 11px;
    font-weight: 700;
    color: var(--text-primary);
  }
  .sub {
    font-size: 9px;
    color: var(--text-ghost);
    flex: 1;
  }
  .reset {
    background: none;
    border: none;
    color: var(--text-ghost);
    font-size: 9px;
    font-family: inherit;
    cursor: pointer;
    padding: 2px 6px;
    border-radius: var(--radius-small);
    transition: color 0.15s ease, background 0.15s ease;
  }
  .reset:hover {
    color: var(--text-primary);
    background: var(--bg-base);
  }

  /* saved-autons picker (open-only) + save-as popover */
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

  .hintbox {
    min-height: 44px;
    background: var(--bg-base);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 7px 10px;
    display: flex;
    flex-direction: column;
    gap: 4px;
  }
  .sig {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 10px;
    color: var(--text-dim);
    line-height: 1.4;
  }
  .sig-name {
    color: var(--text-primary);
    font-weight: 600;
  }
  .sig-rest {
    color: var(--text-ghost);
  }
  .sig-arg.active {
    color: #ecd88f;
    font-weight: 700;
  }
  .doc {
    font-size: 9.5px;
    color: var(--text-secondary);
    line-height: 1.4;
  }
  .doc.quiet {
    color: var(--text-ghost);
  }
  .fields {
    display: flex;
    flex-wrap: wrap;
    gap: 3px 12px;
  }
  .field {
    font-size: 9px;
    color: var(--text-secondary);
  }
  .field code {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9px;
    color: #ecd88f;
  }
  .fdoc {
    color: var(--text-ghost);
  }

  .errors {
    display: flex;
    flex-direction: column;
    gap: 3px;
    max-height: 90px;
    overflow-y: auto;
  }
  .err {
    font-size: 9.5px;
    color: #f0a3a3;
    line-height: 1.35;
  }
  .err.more {
    color: var(--text-ghost);
  }
  .eline {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-weight: 700;
    color: #f87171;
    margin-right: 4px;
  }
</style>
