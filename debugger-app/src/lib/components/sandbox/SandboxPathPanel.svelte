<script lang="ts">
  // Left column replacement for SandboxParams, active whenever the caret sits
  // on a trajectory call or the std::vector<BezierSegment> it
  // references (see +page.svelte). Reproduces the Trajectory Generator's own
  // left column (PursuitParams.svelte) — Path / Transform / Speed plan — over
  // the sandbox's own BezierSegment[] model. The route text stays the single
  // source of truth: every button here calls a store helper that rewrites
  // sandbox.code, then re-derives from the fresh parse.
  import {
    sandbox,
    getRoute,
    activePathDecl,
    segsFromPathDecl,
    pathAddAnchor,
    pathInsertMidpoint,
    pathDeleteAnchor,
    pathMirror,
    pathReverse,
    createSplineFor,
    anchorInfo,
    moveAnchor,
    setAnchorDir,
    setAnchorHandle,
    setAnchorMaxVel,
    pushUndo,
    type MirrorAxis
  } from '$lib/stores/sandbox.svelte';
  import { bezierLength } from '$lib/sandbox/ramsete';
  import SliderRow from './SliderRow.svelte';
  import CoordField from '../pursuit/CoordField.svelte';
  import type { CallStmt } from '$lib/sandbox/parse';

  const route = $derived(getRoute());
  const selIdx = $derived(sandbox.selected);
  const selStmt = $derived(selIdx !== null ? (route.parsed.stmts[selIdx] ?? null) : null);
  const decl = $derived(activePathDecl());

  // ramsete.trajectory(path) is selected but `path` has no declaration yet —
  // the entry point for the whole feature (see createSplineFor's own doc).
  const createCall = $derived.by((): CallStmt | null => {
    if (decl) return null;
    return selStmt?.kind === 'call' && selStmt.fn === 'ramsete.trajectory' ? selStmt : null;
  });
  const createName = $derived.by(() => {
    const a0 = createCall?.args[0];
    return a0?.kind === 'ident' ? a0.name : null;
  });

  const segs = $derived(decl ? segsFromPathDecl(decl) : []);
  const anchorCount = $derived(segs.length + 1);
  const totalLength = $derived(segs.reduce((sum, s) => sum + bezierLength(s), 0));

  // POINTS: the actual baked schedule's row count, from whichever
  // trajectory call on this path has been simulated — the exact same
  // rows the field's speed-coloured polyline draws. Falls back to an estimate
  // off the spacing knob when the path isn't wired to a call yet (or that
  // call baked fewer than 2 rows and produced no segment).
  const pointCount = $derived.by(() => {
    if (!decl) return 0;
    for (let i = 0; i < route.parsed.stmts.length; i++) {
      const s = route.parsed.stmts[i];
      if (s.kind === 'call' && s.fn === 'ramsete.trajectory') {
        const a0 = s.args[0];
        if (a0?.kind === 'ident' && a0.name === decl.name) {
          const found = route.sim.segments.find((se) => se.stmtIndex === i);
          if (found?.traj) return found.traj.rows.length;
        }
      }
    }
    return Math.max(1, Math.ceil(totalLength / Math.max(0.25, sandbox.knobs.rsSpacing)));
  });

  const fmt = (v: number, d = 1) => {
    let s = v.toFixed(d);
    if (s.includes('.')) s = s.replace(/0+$/, '').replace(/\.$/, '');
    return s || '0';
  };

  const MIRRORS: { axis: MirrorAxis; label: string; title: string }[] = [
    { axis: 'horizontal', label: '↔', title: 'Mirror left ↔ right (x → −x)' },
    { axis: 'vertical', label: '↕', title: 'Mirror up ↕ down (y → −y)' },
    { axis: 'diagonal', label: '⤢', title: 'Reflect across the y = x diagonal' },
    { axis: 'antidiagonal', label: '⤡', title: 'Reflect across the y = −x diagonal' }
  ];

  function onAdd() {
    if (decl) pathAddAnchor(decl);
  }
  function onInsertMid() {
    if (decl) pathInsertMidpoint(decl);
  }
  function onDelete() {
    if (decl) pathDeleteAnchor(decl);
  }
  function onMirror(axis: MirrorAxis) {
    if (decl) pathMirror(decl, axis);
  }
  function onRotate() {
    if (decl) pathMirror(decl, 'rotate180');
  }
  function onReverse() {
    if (decl) pathReverse(decl);
  }
  function onCreate() {
    if (selIdx !== null && createCall) createSplineFor(selIdx);
  }

  // --- selected-anchor widget: PursuitParams's per-anchor editor, over the
  // sandbox's BezierSegment[] model (see sandbox.svelte.ts's "selected-anchor
  // widget" section for the mapping). Anchor count changes out from under a
  // stale selection (a structural edit elsewhere) just hides the widget —
  // anchorInfo returns null rather than throwing.
  const anchor = $derived.by(() => {
    if (!decl || sandbox.selectedAnchor === null) return null;
    return anchorInfo(decl, sandbox.selectedAnchor);
  });

  function onDeleteAnchor() {
    if (!decl || !anchor?.isJoint) return;
    // pathDeleteAnchor merges segs[i] and segs[i+1] at joint anchor i+1, so
    // deleting anchor k needs selectedSeg = k-1 — the same field the field's
    // own Delete-anchor gesture (via selectedSeg) already targets.
    sandbox.selectedSeg = anchor.index - 1;
    pathDeleteAnchor(decl);
    sandbox.selectedAnchor = null;
  }

  function onToggleSpeedLimit() {
    if (!decl || !anchor) return;
    setAnchorMaxVel(decl.name, anchor.index, anchor.maxVel >= 0 ? null : Math.round(sandbox.knobs.rsMaxVel / 2));
  }
</script>

<div class="col">
  {#if decl}
    <section class="panel group">
      <div class="gtitle">Path <span class="gsub">cubic bezier spline</span></div>
      <div class="specgrid">
        <div class="spec">
          <span class="slbl">Points</span>
          <div class="sval-row"><span class="sval">{pointCount}</span></div>
        </div>
        <div class="spec">
          <span class="slbl">Length</span>
          <div class="sval-row"><span class="sval">{fmt(totalLength)}</span><span class="sunit">in</span></div>
        </div>
        <div class="spec">
          <span class="slbl">Anchors</span>
          <div class="sval-row"><span class="sval">{anchorCount}</span></div>
        </div>
      </div>
      <div class="btnrow">
        <button class="act" onclick={onAdd}>+ Add anchor</button>
        <button class="act" onclick={onInsertMid}>Insert midpoint</button>
      </div>
      <button class="act danger" disabled={segs.length <= 1} onclick={onDelete} title="Merge a joint's two segments into one">
        Delete anchor
      </button>
      <div class="note">
        Drag anchors and their tangent handles to shape the spline; double-click the curve to insert an anchor, or empty field to extend it.
      </div>
    </section>

    <!-- Selected-anchor widget — PursuitParams.svelte's own per-anchor editor
         (Anchor N: X/Y/Dir tiles, handle sliders, Limit speed here / Delete
         anchor), ported onto one anchor of the
         BezierSegment[] model instead of pursuit's Anchor[]. Only shows once
         a control point of kind a/d has been clicked on the field. -->
    {#if anchor}
      {@const d = decl}
      {@const a = anchor}
      <section class="panel group">
        <div class="gtitle">
          Anchor {a.index + 1} <span class="gsub">{a.isFirst ? 'start pose' : a.isLast ? 'end pose' : 'via'}</span>
        </div>
        <div class="coordgrid">
          <!-- moveAnchor stays push-free in the store (it's shared with the field's
               own control-point drag, which needs the dragDirty single-push
               treatment instead) — so the X/Y tiles push their own undo step here,
               sharing one coalesce key so editing X then Y in one sitting collapses
               into a single ⌘Z (Feature 3). setAnchorDir pushes internally since
               it's never drag-driven. -->
          <CoordField
            label="X"
            value={a.x}
            unit="in"
            onchange={(v) => {
              pushUndo('anchorPos:' + d.name + ':' + a.index);
              moveAnchor(d.name, a.index, v, a.y);
            }}
          />
          <CoordField
            label="Y"
            value={a.y}
            unit="in"
            onchange={(v) => {
              pushUndo('anchorPos:' + d.name + ':' + a.index);
              moveAnchor(d.name, a.index, a.x, v);
            }}
          />
          <CoordField label="Dir" value={a.dir} unit="°" decimals={0} onchange={(v) => setAnchorDir(d.name, a.index, v)} />
        </div>
        {#if a.handleIn !== null}
          <SliderRow label="Handle in" value={a.handleIn} min={3} max={60} step={1} unit="in" onchange={(v) => setAnchorHandle(d.name, a.index, 'in', v)} />
        {/if}
        {#if a.handleOut !== null}
          <SliderRow label="Handle out" value={a.handleOut} min={3} max={60} step={1} unit="in" onchange={(v) => setAnchorHandle(d.name, a.index, 'out', v)} />
        {/if}
        <button
          class="act"
          class:tgl-on={a.maxVel >= 0}
          onclick={onToggleSpeedLimit}
          title="Cap the baked schedule's speed on the segment this anchor starts"
        >
          {a.maxVel >= 0 ? `Speed cap ${fmt(a.maxVel)} in/s` : 'Limit speed here'}
        </button>
        {#if a.maxVel >= 0}
          <SliderRow
            label="Speed cap"
            value={a.maxVel}
            min={0}
            max={sandbox.knobs.rsMaxVel}
            step={1}
            unit="in/s"
            onchange={(v) => setAnchorMaxVel(d.name, a.index, v)}
          />
          <div class="note">
            Sets <span class="mono2">.maxVel</span> on segment {Math.min(a.index, segs.length - 1) + 1} — the piece this anchor starts. There's no
            per-anchor speed in the library, only this per-segment ceiling (bakeTrajectory's speedCeiling).
          </div>
        {/if}
        <button
          class="act danger"
          disabled={!a.isJoint}
          onclick={onDeleteAnchor}
          title={a.isJoint ? "Merge this joint's two segments into one" : 'Endpoints have no joint to remove'}
        >
          Delete anchor
        </button>
      </section>
    {/if}

    <section class="panel group">
      <div class="gtitle">Transform <span class="gsub">mirror · reverse</span></div>
      <div class="mirrorrow">
        {#each MIRRORS as m (m.axis)}
          <button class="mir" title={m.title} onclick={() => onMirror(m.axis)}>{m.label}</button>
        {/each}
        <button class="mir" title="Rotate 180° about the field centre" onclick={onRotate}>180°</button>
      </div>
      <button class="act" onclick={onReverse}>⇄ Reverse direction</button>
      <div class="note">
        Mirror/rotate for the other alliance or side of a symmetric field; reverse flips the segment order without touching each segment's own
        <span class="mono2">.reversed</span> flag.
      </div>
    </section>

    <section class="panel group">
      <div class="gtitle">Speed plan <span class="gsub">baked by bakeTrajectory</span></div>
      <SliderRow label="Max speed" min={5} max={150} step={1} unit="in/s" value={sandbox.knobs.rsMaxVel} onchange={(v) => (sandbox.knobs.rsMaxVel = v)} />
      <SliderRow label="Exit speed" min={0} max={90} step={1} unit="in/s" value={sandbox.knobs.rsEndSpeed} onchange={(v) => (sandbox.knobs.rsEndSpeed = v)} />
      <SliderRow label="Plan accel" min={10} max={300} step={5} unit="in/s²" value={sandbox.knobs.rsMaxAccel} onchange={(v) => (sandbox.knobs.rsMaxAccel = v)} />
      <div class="note">
        These mirror the RAMSETE panel's own chassis knobs. A single segment's own
        ceiling is <span class="mono2">.maxVel</span> on the segment itself.
      </div>
    </section>
  {:else if createName}
    <section class="panel group">
      <div class="gtitle">Path <span class="gsub">cubic bezier spline</span></div>
      <div class="note">
        This <span class="mono2">trajectory</span> call references <span class="mono2">{createName}</span>, which isn't declared above it
        yet. Create spline seeds one quadratic segment: it starts at the robot's simulated pose at this point in the route, leaves along that
        heading, and runs 24 inches out toward the middle of the field; drag it into shape from there.
      </div>
      <button class="act create" onclick={onCreate}>+ Create spline</button>
    </section>
  {:else}
    <section class="panel group">
      <div class="gtitle">Path <span class="gsub">cubic bezier spline</span></div>
      <div class="note">Finish naming the path argument (or declare it above this call) to edit a spline here.</div>
    </section>
  {/if}
</div>

<style>
  .col {
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
  .mono2 {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 8px;
    font-weight: 400;
    color: var(--text-ghost);
  }

  /* selected-anchor widget: X/Y/Dir tiles, same grid PursuitParams uses */
  .coordgrid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(62px, 1fr));
    gap: 8px;
    margin-bottom: 2px;
  }
  /* Limit speed here toggled-on state — cyan (this panel's
     own accent, see .act.create below) instead of PursuitParams's amber, so
     the sandbox's path-editing controls read as one family. */
  .act.tgl-on {
    color: #06b6d4;
    background: rgba(6, 182, 212, 0.12);
  }

  /* POINTS / LENGTH / ANCHORS — same tile treatment as SandboxParams's
     drivetrain spec cards, read-only here (a span, not an input) */
  .specgrid {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 8px;
  }
  .spec {
    display: flex;
    flex-direction: column;
    gap: 6px;
    background: var(--bg-base);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 8px 10px 9px;
    min-width: 0;
  }
  .slbl {
    font-size: 8.5px;
    font-weight: 700;
    letter-spacing: 0.05em;
    text-transform: uppercase;
    color: var(--text-ghost);
  }
  .sval-row {
    display: flex;
    align-items: baseline;
    gap: 4px;
  }
  .sval {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 17px;
    font-weight: 700;
    color: var(--text-primary);
    font-variant-numeric: tabular-nums;
  }
  .sunit {
    font-size: 9px;
    color: var(--text-ghost);
    flex-shrink: 0;
  }

  /* action buttons + mirror row — the Trajectory Generator's own shapes
     (PursuitParams.svelte), accented cyan to match trajectory's
     movement color instead of the generator's amber */
  .btnrow {
    display: flex;
    gap: 6px;
  }
  .btnrow .act {
    flex: 1;
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
  .act.danger:hover:not(:disabled) {
    color: #f87171;
  }
  .act.create {
    color: #06b6d4;
    box-shadow:
      var(--ring),
      inset 0 0 0 1px rgba(6, 182, 212, 0.4);
  }
  .act.create:hover {
    background: rgba(6, 182, 212, 0.12);
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
    color: #06b6d4;
    background: var(--bg-raised);
  }
</style>
