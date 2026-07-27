<script lang="ts">
  import {
    sandbox,
    getRoute,
    resetSandboxCode,
    saveAuton,
    loadAuton,
    deleteAuton,
    refreshSavedAutons
  } from '$lib/stores/sandbox.svelte';
  import { FN_BY_NAME, STRUCT_FIELDS, STRUCT_NAMES } from '$lib/sandbox/spec';
  import SandboxEditor from './SandboxEditor.svelte';

  const route = $derived(getRoute());
  const errors = $derived(route.parsed.errors);

  let caret = $state(0);

  // --- saved autons (files in the "sandbox autons" folder) ---
  let savedOpen = $state(false);
  let saveName = $state('');
  // load the folder listing once on mount, and whenever the drawer opens
  $effect(() => {
    refreshSavedAutons();
  });
  function doSave() {
    if (!saveName.trim()) return;
    saveAuton(saveName);
    savedOpen = true;
  }
  function fmtDate(ms: number): string {
    return new Date(ms).toLocaleDateString(undefined, { month: 'short', day: 'numeric' });
  }

  // Track which statement the caret sits in → drives selection on the field.
  $effect(() => {
    const c = caret;
    const stmts = route.parsed.stmts;
    for (let i = 0; i < stmts.length; i++) {
      if (c >= stmts[i].start && c <= stmts[i].end) {
        sandbox.selected = i;
        return;
      }
    }
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
      if (f.name === 'tOut') return k.ecLatTOut;
      if (f.name === 'setR') return k.ecLatSetR;
      if (f.name === 'setD') return k.ecLatSetD;
      if (f.name === 'crossD') return k.ecLatCrossD;
    } else if (structKind === 'turnEc') {
      if (f.name === 'tOut') return k.ecTurnTOut;
      if (f.name === 'setR') return k.ecTurnSetR;
      if (f.name === 'setD') return k.ecTurnSetD;
      if (f.name === 'extA') return k.ecTurnExtA;
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

<section class="panel codepanel">
  <div class="head">
    <span class="lbl">Route code</span>
    <span class="sub">LucasLib syntax · paste-ready</span>
    <button
      class="reset"
      class:on={savedOpen}
      onclick={() => {
        savedOpen = !savedOpen;
        if (savedOpen) refreshSavedAutons();
      }}
      title="Saved autons (sandbox autons folder)"
    >
      saved{sandbox.saved.length ? ` (${sandbox.saved.length})` : ''}
    </button>
    <button class="reset" onclick={resetSandboxCode} title="Replace with the starter route">reset</button>
  </div>

  {#if savedOpen}
    <div class="savedbar">
      <form
        class="saverow"
        onsubmit={(e) => {
          e.preventDefault();
          doSave();
        }}
      >
        <input class="savename" placeholder="save current as…" bind:value={saveName} />
        <button class="savebtn" type="submit" disabled={!saveName.trim()}>save</button>
      </form>
      {#if sandbox.saved.length === 0}
        <div class="saved-empty">No autons in the folder yet.</div>
      {:else}
        <div class="savedlist">
          {#each sandbox.saved as a (a.name)}
            <div class="savedrow">
              <button class="savedload" onclick={() => loadAuton(a.name)} title="Open into the editor">
                <span class="sname">{a.name}</span>
                <span class="sdate">{fmtDate(a.savedAt)}</span>
              </button>
              <button class="supdate" onclick={() => saveAuton(a.name)} title="Overwrite this file with the current route">↻</button>
              <button class="sdel" onclick={() => deleteAuton(a.name)} title="Delete the file">×</button>
            </div>
          {/each}
        </div>
      {/if}
    </div>
  {/if}

  <SandboxEditor
    value={sandbox.code}
    parsed={route.parsed}
    oninput={(v) => (sandbox.code = v)}
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
  .reset:hover,
  .reset.on {
    color: var(--text-primary);
    background: var(--bg-base);
  }

  /* saved autos drawer */
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
  .supdate,
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
  .supdate:hover {
    color: var(--accent-text);
  }
  .sdel:hover {
    color: #f87171;
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
