<script lang="ts">
  // Left panel — planner knobs, styled like the sandbox param cards.
  // "Path shape" knobs re-bake the array; "Follow" knobs export into the call.
  import SliderRow from '../sandbox/SliderRow.svelte';
  import CoordField from './CoordField.svelte';
  import {
    pursuit,
    viewOpts,
    history,
    getPlan,
    deleteAnchor,
    editAnchor,
    setParam,
    undo,
    redo,
    setSegmentCap,
    setSegmentStop,
    addAnchorEnd,
    insertMidpoint,
    mirrorPath,
    reversePath,
    type MirrorAxis
  } from '$lib/stores/pursuit.svelte';

  const plan = $derived(getPlan());
  const sel = $derived(pursuit.selected !== null ? pursuit.anchors[pursuit.selected] ?? null : null);

  const MIRRORS: { axis: MirrorAxis; label: string; title: string }[] = [
    { axis: 'vertical', label: '↔', title: 'Mirror left ↔ right (across x = 0)' },
    { axis: 'horizontal', label: '↕', title: 'Mirror up ↕ down (across y = 0)' },
    { axis: 'diag', label: '⤢', title: 'Reflect across the y = x diagonal' },
    { axis: 'antidiag', label: '⤡', title: 'Reflect across the y = −x diagonal' },
    { axis: 'rotate', label: '180°', title: 'Rotate 180° about the field centre' }
  ];

  function set<K extends keyof typeof pursuit.params>(k: K, v: number) {
    setParam(k, v);
  }

  const SNAPS = [
    { v: 0, l: 'Off' },
    { v: 24, l: '24″' },
    { v: 12, l: '12″' },
    { v: 6, l: '6″' }
  ];

  // Tangent-handle direction snap. Same shape as the grid snap: 0 is off, the
  // rest are the angles a route is actually authored against.
  const ANGLE_SNAPS = [
    { v: 0, l: 'Off' },
    { v: 15, l: '15°' },
    { v: 45, l: '45°' },
    { v: 90, l: '90°' }
  ];
</script>

<div class="col">
  <div class="toolbar">
    <div class="tbtns">
      <button class="tb" disabled={!history.canUndo} onclick={undo} title="Undo (⌘Z)" aria-label="Undo">↶</button>
      <button class="tb" disabled={!history.canRedo} onclick={redo} title="Redo (⌘⇧Z)" aria-label="Redo">↷</button>
    </div>
    <div class="snap" role="group" aria-label="Grid snap">
      <span class="snlbl">Snap</span>
      {#each SNAPS as s}
        <button class="sn" class:on={viewOpts.snap === s.v} onclick={() => (viewOpts.snap = s.v)}>{s.l}</button>
      {/each}
    </div>
    <div class="snap" role="group" aria-label="Handle angle snap">
      <span class="snlbl">Angle</span>
      {#each ANGLE_SNAPS as s}
        <button class="sn" class:on={viewOpts.angleSnap === s.v} onclick={() => (viewOpts.angleSnap = s.v)}>{s.l}</button>
      {/each}
    </div>
  </div>

  <section class="panel group">
    <div class="gtitle">Path <span class="gsub">cubic bezier spline</span></div>
    <div class="statgrid">
      <div class="stat">
        <span class="slbl">points</span>
        <span class="sval">{plan.points.length}</span>
      </div>
      <div class="stat">
        <span class="slbl">length</span>
        <span class="sval">{plan.total.toFixed(1)}<span class="sunit">in</span></span>
      </div>
      <div class="stat">
        <span class="slbl">anchors</span>
        <span class="sval">{pursuit.anchors.length}</span>
      </div>
    </div>
    <div class="btnrow">
      <button class="act" onclick={addAnchorEnd}>+ Add anchor</button>
      <button class="act" onclick={insertMidpoint}>Insert midpoint</button>
    </div>
    <div class="note">Drag anchors + their gold tangent handles to shape the spline. Double-click the curve to insert an anchor, double-click empty field to extend.</div>
  </section>

  <section class="panel group">
    <div class="gtitle">Transform <span class="gsub">mirror · reverse</span></div>
    <div class="mirrorrow">
      {#each MIRRORS as m}
        <button class="mir" title={m.title} onclick={() => mirrorPath(m.axis)}>{m.label}</button>
      {/each}
    </div>
    <button class="act" onclick={reversePath}>⇄ Reverse direction</button>
    <div class="note">Mirror across a field axis for the other alliance — the diagonals cover VEX's diagonally/rotationally symmetric fields. Reverse swaps start ↔ end and flips every tangent.</div>
  </section>

  <section class="panel group">
    <div class="gtitle">Bake <span class="gsub">ProfileConfig — profile.h</span></div>
    <SliderRow label="Max speed" value={pursuit.params.maxVel} min={10} max={90} step={1} unit="in/s" onchange={(v) => set('maxVel', v)} />
    <SliderRow label="Plan accel" value={pursuit.params.maxAccel} min={10} max={200} step={5} unit="in/s²" onchange={(v) => set('maxAccel', v)} />
    <SliderRow label="velMargin" value={pursuit.params.velMargin} min={0.1} max={1} step={0.01} decimals={2} onchange={(v) => set('velMargin', v)} />
    <SliderRow label="Exit speed" value={pursuit.params.endSpeed} min={0} max={90} step={1} unit="in/s" onchange={(v) => set('endSpeed', v)} />
  </section>

  {#if sel && pursuit.selected !== null}
    {@const i = pursuit.selected}
    <section class="panel group">
      <div class="gtitle">Anchor {i + 1} <span class="gsub">{i === 0 ? 'start pose' : i === pursuit.anchors.length - 1 ? 'end pose' : 'via'}</span></div>
      <div class="coordgrid">
        <CoordField label="X" value={sel.x} unit="in" onchange={(v) => editAnchor(i, { x: v })} />
        <CoordField label="Y" value={sel.y} unit="in" onchange={(v) => editAnchor(i, { y: v })} />
        <CoordField label={i === 0 ? 'Hdg' : 'Dir'} value={sel.ang} unit="°" decimals={0} onchange={(v) => editAnchor(i, { ang: v })} />
      </div>
      {#if i > 0}
        <SliderRow label="Handle in" value={sel.in} min={3} max={60} step={1} unit="in" onchange={(v) => editAnchor(i, { in: v })} />
      {/if}
      {#if i < pursuit.anchors.length - 1}
        <SliderRow label="Handle out" value={sel.out} min={3} max={60} step={1} unit="in" onchange={(v) => editAnchor(i, { out: v })} />
      {/if}
      {#if i < pursuit.anchors.length - 1}
        <button
          class="act"
          class:tgl-on={sel.segMaxVel !== undefined}
          onclick={() => setSegmentCap(i, sel.segMaxVel === undefined ? Math.round(pursuit.params.maxVel / 2) : null)}
          title="Cap BezierSegment.maxVel for the segment leaving this anchor"
        >
          {sel.segMaxVel === undefined ? `Cap segment ${i + 1}` : `Segment ${i + 1} ≤ ${sel.segMaxVel} in/s`}
        </button>
        {#if sel.segMaxVel !== undefined}
          <SliderRow label=".maxVel" value={sel.segMaxVel} min={0} max={pursuit.params.maxVel} step={1} unit="in/s" onchange={(v) => setSegmentCap(i, v)} />
        {/if}
        <button
          class="act"
          class:tgl-on={!!sel.segStopEnd}
          onclick={() => setSegmentStop(i, !sel.segStopEnd)}
          title="BezierSegment.stopEnd — force a full stop where this segment ends"
        >
          {sel.segStopEnd ? '⏹ Stops at segment end' : 'Stop at segment end'}
        </button>
        <div class="note">
          A cap covers the whole segment. To slow only part of a curve, insert an anchor at each end of the
          stretch — the split reproduces the curve exactly and a joint costs no speed — then cap the middle piece.
        </div>
      {/if}
      <button class="act danger" disabled={pursuit.anchors.length <= 2} onclick={() => deleteAnchor(i)}>
        Delete anchor
      </button>
    </section>
  {/if}
</div>

<style>
  .col {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: var(--gap);
    min-height: 0;
    overflow-y: auto;
  }
  .group {
    padding: 12px 14px;
    display: flex;
    flex-direction: column;
    gap: 8px;
    flex-shrink: 0;
  }
  .gtitle {
    font-size: 11px;
    font-weight: 700;
    color: var(--text-primary);
    display: flex;
    align-items: baseline;
    gap: 6px;
    flex-wrap: wrap;
  }
  .gsub {
    font-size: 8.5px;
    font-weight: 400;
    color: var(--text-ghost);
  }
  .note {
    font-size: 8.5px;
    color: var(--text-ghost);
    line-height: 1.45;
    margin: -2px 0 2px;
  }

  .statgrid {
    display: grid;
    grid-template-columns: 1fr 1.4fr 1fr;
    gap: 8px;
    margin-bottom: 2px;
  }
  .stat {
    display: flex;
    flex-direction: column;
    gap: 3px;
    background: var(--bg-base);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 7px 10px 8px;
    min-width: 0;
  }
  .slbl {
    font-size: 8.5px;
    font-weight: 700;
    letter-spacing: 0.05em;
    text-transform: uppercase;
    color: var(--text-ghost);
  }
  .sval {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 15px;
    font-weight: 700;
    color: var(--text-primary);
    font-variant-numeric: tabular-nums;
    display: flex;
    align-items: baseline;
    gap: 3px;
  }
  .sunit {
    font-size: 9px;
    font-weight: 400;
    color: var(--text-dim);
  }

  .coordgrid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(62px, 1fr));
    gap: 8px;
    margin-bottom: 2px;
  }

  /* top toolbar: undo/redo + grid snap */
  .toolbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 8px;
    flex-shrink: 0;
  }
  .tbtns {
    display: flex;
    gap: 4px;
  }
  .tb {
    width: 26px;
    height: 24px;
    border: none;
    border-radius: var(--radius-small);
    background: var(--bg-surface);
    box-shadow: var(--ring);
    color: var(--text-secondary);
    font-size: 14px;
    line-height: 1;
    cursor: pointer;
    transition: color 0.12s ease, background 0.12s ease;
  }
  .tb:hover:not(:disabled) {
    color: var(--text-primary);
    background: var(--bg-raised);
  }
  .tb:disabled {
    opacity: 0.3;
    cursor: default;
  }
  .snap {
    display: flex;
    align-items: center;
    gap: 3px;
  }
  .snlbl {
    font-size: 8.5px;
    font-weight: 700;
    letter-spacing: 0.05em;
    text-transform: uppercase;
    color: var(--text-ghost);
    margin-right: 2px;
  }
  .sn {
    padding: 4px 7px;
    border: none;
    border-radius: var(--radius-small);
    background: var(--bg-surface);
    box-shadow: var(--ring);
    color: var(--text-muted);
    font-family: inherit;
    font-size: 9px;
    font-weight: 600;
    cursor: pointer;
    transition: color 0.12s ease, background 0.12s ease;
  }
  .sn:hover {
    color: var(--text-primary);
  }
  .sn.on {
    color: var(--bg-base);
    background: #fbbf24;
  }

  .btnrow {
    display: flex;
    gap: 6px;
  }
  .btnrow .act {
    flex: 1;
  }
  .act.tgl-on {
    color: #fbbf24;
    background: rgba(251, 191, 36, 0.12);
  }

  .warns {
    display: flex;
    flex-direction: column;
    gap: 5px;
    flex-shrink: 0;
  }
  .warn {
    font-size: 9px;
    line-height: 1.4;
    color: #fca5a5;
    background: rgba(69, 10, 10, 0.5);
    box-shadow: inset 0 0 0 1px rgba(248, 113, 113, 0.28);
    border-radius: var(--radius-small);
    padding: 6px 9px;
  }

  .chiprow {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 6px;
  }
  .chip {
    padding: 7px 0;
    border: none;
    border-radius: var(--radius-small);
    background: var(--bg-base);
    box-shadow: var(--ring);
    color: var(--text-secondary);
    font-family: inherit;
    font-size: 10px;
    font-weight: 600;
    cursor: pointer;
    transition: color 0.12s ease, background 0.12s ease;
  }
  .chip:hover {
    color: #fbbf24;
    background: var(--bg-raised);
  }

  .mirrorrow {
    display: flex;
    gap: 6px;
  }
  .mir {
    flex: 1;
    padding: 7px 0;
    border: none;
    border-radius: var(--radius-small);
    background: var(--bg-base);
    box-shadow: var(--ring);
    color: var(--text-secondary);
    font-family: inherit;
    font-size: 12px;
    font-weight: 700;
    cursor: pointer;
    transition: color 0.12s ease, background 0.12s ease;
  }
  .mir:hover {
    color: #fbbf24;
    background: var(--bg-raised);
  }

  .act {
    flex-shrink: 0;
    padding: 7px 10px;
    font-family: inherit;
    font-size: 10px;
    font-weight: 600;
    color: var(--text-secondary);
    background: var(--bg-surface);
    border: none;
    box-shadow: var(--ring);
    border-radius: var(--radius-small);
    cursor: pointer;
    transition: color 0.15s ease, background 0.15s ease;
  }
  .act:hover:not(:disabled) {
    color: var(--text-primary);
    background: var(--bg-raised);
  }
  .act:disabled {
    opacity: 0.4;
    cursor: default;
  }
  .act.danger {
    background: var(--bg-base);
  }
  .act.danger:hover:not(:disabled) {
    color: #f87171;
  }
</style>
