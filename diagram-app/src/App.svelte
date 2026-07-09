<script lang="ts">
  import Toolbar from '$lib/components/Toolbar.svelte';
  import Canvas from '$lib/components/Canvas.svelte';
  import Inspector from '$lib/components/Inspector.svelte';
  import {
    ui,
    doc,
    library,
    undo,
    redo,
    deleteSelected,
    duplicateSelected,
    snapshot,
    selectedElements,
    clearSelection,
    newDiagram,
    switchDiagram,
    renameDiagram,
    duplicateDiagram,
    deleteDiagram,
    activeMeta,
    saveToFile,
    openFromFile,
    type Tool
  } from '$lib/model/scene.svelte';
  import { downloadPNG, copyPNG } from '$lib/render/exportPng';
  import { FONTS, fontStack } from '$lib/model/fonts';

  let exportScale = $state(20);
  let copied = $state(false);
  let fileInput: HTMLInputElement;

  function renameCurrent() {
    const n = prompt('Rename design', activeMeta().name);
    if (n) renameDiagram(library.activeId, n);
  }
  function deleteCurrent() {
    if (confirm(`Delete design "${activeMeta().name}"? This can't be undone.`)) deleteDiagram(library.activeId);
  }
  async function onOpenFile(e: Event) {
    const input = e.currentTarget as HTMLInputElement;
    const f = input.files?.[0];
    if (f) {
      const ok = await openFromFile(f);
      if (!ok) alert('Could not read that file — expected a .diagram.json.');
    }
    input.value = '';
  }

  async function doCopy() {
    const ok = await copyPNG({ scale: exportScale });
    copied = ok;
    setTimeout(() => (copied = false), 1400);
  }

  function resetView() {
    const wrap = document.querySelector('.wrap') as HTMLElement;
    ui.view = { tx: (wrap?.clientWidth ?? 800) / 2, ty: (wrap?.clientHeight ?? 600) / 2, s: 4 };
  }

  const shortcuts: Record<string, Tool> = {
    v: 'select',
    r: 'robot',
    l: 'straight',
    a: 'arc',
    s: 'spline',
    b: 'boomerang',
    c: 'circle',
    t: 'text'
  };

  function onKey(e: KeyboardEvent) {
    const el = document.activeElement;
    const typing = el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.tagName === 'SELECT');
    const mod = e.metaKey || e.ctrlKey;

    if (mod && e.key.toLowerCase() === 'z') {
      e.preventDefault();
      e.shiftKey ? redo() : undo();
      return;
    }
    if (mod && e.key.toLowerCase() === 'y') {
      e.preventDefault();
      redo();
      return;
    }
    if (mod && e.key.toLowerCase() === 'd') {
      e.preventDefault();
      duplicateSelected();
      return;
    }
    if (typing) return;

    if (e.key === 'Delete' || e.key === 'Backspace') {
      e.preventDefault();
      deleteSelected();
      return;
    }
    if (e.key === 'Escape') {
      ui.tool = 'select';
      clearSelection();
      return;
    }
    if (e.key.startsWith('Arrow')) {
      const sel = selectedElements();
      if (!sel.length) return;
      e.preventDefault();
      snapshot();
      const step = e.shiftKey ? 5 : 0.5;
      const dx = e.key === 'ArrowRight' ? step : e.key === 'ArrowLeft' ? -step : 0;
      const dy = e.key === 'ArrowUp' ? step : e.key === 'ArrowDown' ? -step : 0;
      for (const s of sel) {
        if (s.type === 'path') s.points = s.points.map((p) => ({ x: p.x + dx, y: p.y + dy }));
        else {
          (s as any).x += dx;
          (s as any).y += dy;
        }
      }
      return;
    }
    const t = shortcuts[e.key.toLowerCase()];
    if (t) ui.tool = t;
  }
</script>

<svelte:window onkeydown={onKey} />

<div class="app">
  <header>
    <div class="brand">Diagram Maker</div>

    <div class="seg designs">
      <select
        class="design-select"
        title="Switch design"
        value={library.activeId}
        onchange={(e) => switchDiagram((e.currentTarget as HTMLSelectElement).value)}
      >
        {#each library.diagrams as d (d.id)}
          <option value={d.id}>{d.name}</option>
        {/each}
      </select>
      <button title="New design" onclick={() => newDiagram()}>＋</button>
      <button title="Rename design" onclick={renameCurrent}>✎</button>
      <button title="Duplicate design" onclick={() => duplicateDiagram(library.activeId)}>⧉</button>
      <button class="ghost" title="Delete design" onclick={deleteCurrent}>🗑</button>
    </div>

    <div class="seg">
      <button title="Save design to a .json file" onclick={saveToFile}>Save</button>
      <button title="Open a .json design file" onclick={() => fileInput.click()}>Open</button>
      <input bind:this={fileInput} type="file" accept=".json,application/json" onchange={onOpenFile} style="display:none" />
    </div>

    <div class="seg">
      <button title="Undo (⌘Z)" onclick={undo}>↶</button>
      <button title="Redo (⌘⇧Z)" onclick={redo}>↷</button>
    </div>

    <div class="seg">
      <span class="lab">Backdrop</span>
      <select bind:value={ui.backdrop}>
        <option value="paper">Paper</option>
        <option value="dark">Dark</option>
        <option value="checker">Transparent</option>
      </select>
    </div>

    <div class="seg">
      <span class="lab">Font</span>
      <select bind:value={ui.font} title="Font for all text & labels">
        {#each FONTS as f}
          <option value={f.name} style="font-family:{f.stack}">{f.name}</option>
        {/each}
      </select>
    </div>

    <div class="seg">
      <button class:on={ui.showGrid} onclick={() => (ui.showGrid = !ui.showGrid)}>Grid</button>
      <button class:on={ui.showField} onclick={() => (ui.showField = !ui.showField)}>Field</button>
      <button class:on={ui.snap} onclick={() => (ui.snap = !ui.snap)}>Snap</button>
      <button onclick={resetView} title="Reset view">Fit</button>
    </div>

    <div class="spacer"></div>

    <div class="seg export">
      <span class="lab">PNG</span>
      <select bind:value={exportScale} title="Export resolution (pixels per inch)">
        <option value={12}>Standard</option>
        <option value={20}>High</option>
        <option value={32}>Max</option>
      </select>
      <button class="primary" onclick={() => downloadPNG(activeMeta().name, { scale: exportScale })}>Download</button>
      <button onclick={doCopy}>{copied ? 'Copied ✓' : 'Copy'}</button>
    </div>
  </header>

  <div class="body">
    <Toolbar />
    <Canvas />
    <Inspector />
  </div>
</div>

<style>
  .app {
    display: flex;
    flex-direction: column;
    height: 100vh;
    width: 100vw;
    overflow: hidden;
  }
  header {
    display: flex;
    align-items: center;
    gap: 12px;
    height: 46px;
    padding: 0 12px;
    background: var(--panel);
    border-bottom: 1px solid var(--border);
    flex: none;
    overflow-x: auto;
    overflow-y: hidden;
    scrollbar-width: none;
  }
  header::-webkit-scrollbar {
    display: none;
  }
  .brand {
    font-weight: 700;
    letter-spacing: -0.01em;
    margin-right: 6px;
  }
  .seg {
    display: flex;
    align-items: center;
    gap: 5px;
  }
  .lab {
    font-size: 11px;
    color: var(--text-faint);
    text-transform: uppercase;
    letter-spacing: 0.05em;
  }
  .spacer {
    flex: 1;
  }
  header button {
    background: var(--panel-2);
    border: 1px solid var(--border);
    color: var(--text-dim);
    border-radius: 6px;
    padding: 5px 9px;
    font-size: 12px;
  }
  header button:hover {
    color: var(--text);
    border-color: var(--border-strong);
  }
  header button.on {
    background: var(--accent-soft);
    border-color: var(--accent);
    color: var(--text);
  }
  header .primary {
    background: var(--accent);
    border-color: var(--accent);
    color: #fff;
  }
  header .primary:hover {
    filter: brightness(1.08);
  }
  header select {
    background: var(--panel-2);
    border: 1px solid var(--border);
    color: var(--text);
    border-radius: 6px;
    padding: 4px 6px;
    font-size: 12px;
  }
  .designs {
    padding-right: 12px;
    margin-right: 4px;
    border-right: 1px solid var(--border);
  }
  .design-select {
    min-width: 116px;
    max-width: 190px;
    font-weight: 600;
  }
  .designs button,
  header .ghost {
    padding: 5px 7px;
  }
  header .ghost:hover {
    color: var(--danger);
    border-color: var(--danger);
  }
  .body {
    flex: 1;
    display: flex;
    min-height: 0;
  }
</style>
