<script lang="ts">
  import {
    selectedElements,
    deleteSelected,
    duplicateSelected,
    bringToFront,
    sendToBack,
    snapshot,
    addGhostsAlong
  } from '$lib/model/scene.svelte';
  import type { PathEl, PathKind } from '$lib/model/types';
  import { samplePath } from '$lib/geom/paths';
  import { createPath, defaultChassis, INK } from '$lib/model/defaults';
  import ColorField from './ColorField.svelte';
  import ChassisEditor from './ChassisEditor.svelte';

  const sel = $derived(selectedElements());
  const one = $derived(sel.length === 1 ? sel[0] : null);

  let armed = true;
  function arm() {
    if (armed) {
      snapshot();
      armed = false;
    }
  }
  function disarm() {
    armed = true;
  }

  // ---- path helpers ----
  function convertKind(el: PathEl, kind: PathKind) {
    snapshot();
    const a = el.points[0];
    const b = el.points[el.points.length - 1];
    const fresh = createPath(kind, a, b);
    el.kind = kind;
    el.points = fresh.points;
    el.headingStart = fresh.headingStart;
    el.headingEnd = fresh.headingEnd ?? null;
    el.boom = fresh.boom;
  }
  function addPoint(el: PathEl) {
    snapshot();
    const n = el.points.length;
    const mid = { x: (el.points[n - 2].x + el.points[n - 1].x) / 2, y: (el.points[n - 2].y + el.points[n - 1].y) / 2 };
    el.points.splice(n - 1, 0, mid);
  }
  function removePoint(el: PathEl) {
    if (el.points.length <= 2) return;
    snapshot();
    el.points.splice(1, 1);
  }
  function toggleEndHeading(el: PathEl) {
    snapshot();
    if (el.headingEnd == null) {
      const n = el.points.length;
      const a = el.points[n - 2];
      const b = el.points[n - 1];
      el.headingEnd = (Math.atan2(b.x - a.x, b.y - a.y) * 180) / Math.PI;
    } else {
      el.headingEnd = null;
    }
  }

  const pathLen = $derived(one && one.type === 'path' ? samplePath(one).totalLen : 0);

  // ---- ghosts ----
  let ghostN = $state(5);
  let ghostFade = $state<'end' | 'start' | 'none'>('end');
  function doGhosts(el: PathEl) {
    const chassis = el.kind === 'boomerang' && el.boom ? el.boom.chassis : defaultChassis();
    addGhostsAlong(el, ghostN, ghostFade, chassis, INK);
  }

  function setScrub(el: PathEl, on: boolean) {
    if (!el.boom) return;
    snapshot();
    el.boom.robotT = on ? 0.5 : null;
  }
</script>

<div class="inspector">
  {#if sel.length === 0}
    <div class="empty">
      <p>Nothing selected.</p>
      <p class="dim">Pick a tool on the left and draw, or select an element to edit it.</p>
    </div>
  {:else}
    <div class="topbar">
      <span class="kind">{sel.length > 1 ? `${sel.length} selected` : one?.type}</span>
      <div class="acts">
        <button title="Duplicate (⌘D)" onclick={duplicateSelected}>⧉</button>
        <button title="Bring to front" onclick={bringToFront}>⤒</button>
        <button title="Send to back" onclick={sendToBack}>⤓</button>
        <button class="danger" title="Delete (⌫)" onclick={deleteSelected}>🗑</button>
      </div>
    </div>

    <!-- opacity (all) -->
    <div class="section">
      <label class="row">
        <span>Opacity</span>
        <input
          type="range"
          min="0.05"
          max="1"
          step="0.05"
          value={one ? one.opacity : 1}
          onpointerdown={arm}
          oninput={(e) => sel.forEach((s) => (s.opacity = +(e.target as HTMLInputElement).value))}
          onchange={disarm}
        />
      </label>
    </div>

    {#if one}
      {#if one.type === 'robot'}
        <div class="section">
          <div class="hdr">Robot</div>
          <label class="row"><span>Color</span></label>
          <ColorField bind:value={one.color} />
          <label class="row checkbox"><input type="checkbox" bind:checked={one.fill} onchange={() => arm()} /> <span>Body tint</span></label>
          <label class="row"><span>Heading°</span><input type="number" step="1" bind:value={one.heading} onpointerdown={arm} onchange={disarm} /></label>
          <label class="row"><span>X (in)</span><input type="number" step="0.5" bind:value={one.x} onpointerdown={arm} onchange={disarm} /></label>
          <label class="row"><span>Y (in)</span><input type="number" step="0.5" bind:value={one.y} onpointerdown={arm} onchange={disarm} /></label>
          <label class="row"><span>Label</span><input type="text" bind:value={one.label} placeholder="(none)" onpointerdown={arm} onchange={disarm} /></label>
        </div>
        <div class="section">
          <div class="hdr">Chassis</div>
          <ChassisEditor chassis={one.chassis} color={one.color} />
        </div>
      {:else if one.type === 'path'}
        <div class="section">
          <div class="hdr">Line — {(pathLen).toFixed(1)} in</div>
          <label class="row">
            <span>Type</span>
            <select value={one.kind} onchange={(e) => convertKind(one as PathEl, (e.target as HTMLSelectElement).value as PathKind)}>
              <option value="straight">Straight</option>
              <option value="arc">Arc</option>
              <option value="spline">Spline</option>
              <option value="splineHeading">Spline · heading</option>
              <option value="boomerang">Boomerang</option>
            </select>
          </label>
          <label class="row"><span>Color</span></label>
          <ColorField bind:value={one.color} />
          <label class="row"><span>Width</span><input type="range" min="0.4" max="4" step="0.1" bind:value={one.width} onpointerdown={arm} onchange={disarm} /></label>
          <label class="row">
            <span>Dash</span>
            <select bind:value={one.dash} onpointerdown={arm} onchange={disarm}>
              <option value="solid">Solid</option>
              <option value="dashed">Dashed</option>
              <option value="dotted">Dotted</option>
            </select>
          </label>
          <label class="row">
            <span>Arrows</span>
            <select bind:value={one.arrows} onpointerdown={arm} onchange={disarm}>
              <option value="none">None</option>
              <option value="start">Start</option>
              <option value="end">End</option>
              <option value="both">Both</option>
            </select>
          </label>

          {#if one.kind === 'spline' || one.kind === 'splineHeading'}
            <label class="row"><span>Control pts ({one.points.length})</span>
              <span class="btnpair"><button onclick={() => removePoint(one as PathEl)}>−</button><button onclick={() => addPoint(one as PathEl)}>+</button></span>
            </label>
          {/if}
          {#if one.kind === 'splineHeading'}
            <label class="row checkbox"><input type="checkbox" checked={one.headingEnd != null} onchange={() => toggleEndHeading(one as PathEl)} /> <span>Fix end heading</span></label>
          {/if}

          {#if one.kind === 'boomerang' && one.boom}
            <div class="hdr sub">Boomerang</div>
            <label class="row"><span>Lead</span><input type="range" min="0" max="1" step="0.02" bind:value={one.boom.lead} onpointerdown={arm} onchange={disarm} /><em>{one.boom.lead.toFixed(2)}</em></label>
            <label class="row"><span>Ghost lead</span><input type="range" min="0" max="1" step="0.02" bind:value={one.boom.gLead} onpointerdown={arm} onchange={disarm} /><em>{one.boom.gLead.toFixed(2)}</em></label>
            <label class="row"><span>Start°</span><input type="number" step="1" bind:value={one.boom.startHeading} onpointerdown={arm} onchange={disarm} /></label>
            <label class="row"><span>Target°</span><input type="number" step="1" bind:value={one.boom.targetHeading} onpointerdown={arm} onchange={disarm} /></label>
            <label class="row checkbox"><input type="checkbox" bind:checked={one.boom.backward} onchange={() => arm()} /> <span>Backward</span></label>
            <label class="row checkbox"><input type="checkbox" bind:checked={one.boom.showLeads} onchange={() => arm()} /> <span>Show leads</span></label>
            <label class="row checkbox"><input type="checkbox" checked={one.boom.robotT != null} onchange={(e) => setScrub(one as PathEl, (e.target as HTMLInputElement).checked)} /> <span>Robot on path</span></label>
            {#if one.boom.robotT != null}
              <label class="row"><span>Scrub</span><input type="range" min="0" max="1" step="0.01" bind:value={one.boom.robotT} onpointerdown={arm} onchange={disarm} /><em>{Math.round(one.boom.robotT * 100)}%</em></label>
              <div class="mini"><ChassisEditor chassis={one.boom.chassis} color={one.color} /></div>
            {/if}
          {/if}

          <div class="hdr sub">Ghost robots along path</div>
          <label class="row"><span>Count</span><input type="number" min="2" max="20" bind:value={ghostN} /></label>
          <label class="row">
            <span>Fade</span>
            <select bind:value={ghostFade}>
              <option value="end">Bright at start</option>
              <option value="start">Bright at end</option>
              <option value="none">No fade</option>
            </select>
          </label>
          <button class="wide" onclick={() => doGhosts(one as PathEl)}>Drop {ghostN} robots along path</button>
        </div>
      {:else if one.type === 'shape'}
        <div class="section">
          <div class="hdr">{one.shape === 'circle' ? 'Circle' : 'Rectangle'}</div>
          <label class="row"><span>Stroke</span></label>
          <ColorField bind:value={one.stroke} />
          <label class="row"><span>Fill</span></label>
          <ColorField bind:value={one.fill} allowNone={true} />
          <label class="row"><span>Stroke w</span><input type="range" min="0.3" max="4" step="0.1" bind:value={one.strokeWidth} onpointerdown={arm} onchange={disarm} /></label>
          <label class="row">
            <span>Dash</span>
            <select bind:value={one.dash} onpointerdown={arm} onchange={disarm}>
              <option value="solid">Solid</option>
              <option value="dashed">Dashed</option>
              <option value="dotted">Dotted</option>
            </select>
          </label>
          <label class="row"><span>{one.shape === 'circle' ? 'Diameter' : 'Width'}</span><input type="number" step="0.5" bind:value={one.w} onpointerdown={arm} onchange={disarm} /></label>
          {#if one.shape === 'rect'}
            <label class="row"><span>Height</span><input type="number" step="0.5" bind:value={one.h} onpointerdown={arm} onchange={disarm} /></label>
          {/if}
        </div>
      {:else if one.type === 'text'}
        <div class="section">
          <div class="hdr">Text</div>
          <textarea rows="3" bind:value={one.text} onpointerdown={arm} onchange={disarm}></textarea>
          <label class="row"><span>Color</span></label>
          <ColorField bind:value={one.color} />
          <label class="row"><span>Size</span><input type="range" min="2" max="24" step="0.5" bind:value={one.size} onpointerdown={arm} onchange={disarm} /></label>
          <div class="row">
            <span>Style</span>
            <div class="btnpair">
              <button class:on={one.bold} onclick={() => { arm(); one.bold = !one.bold; disarm(); }}><b>B</b></button>
              <button class:on={one.italic} onclick={() => { arm(); one.italic = !one.italic; disarm(); }}><i>I</i></button>
            </div>
          </div>
          <label class="row">
            <span>Align</span>
            <select bind:value={one.align} onpointerdown={arm} onchange={disarm}>
              <option value="left">Left</option>
              <option value="center">Center</option>
              <option value="right">Right</option>
            </select>
          </label>
          <label class="row"><span>Rotation°</span><input type="number" step="1" bind:value={one.rotation} onpointerdown={arm} onchange={disarm} /></label>
        </div>
      {:else if one.type === 'stamp'}
        <div class="section">
          <div class="hdr">Goal stamp</div>
          <label class="row">
            <span>Color</span>
            <select bind:value={one.kind} onpointerdown={arm} onchange={disarm}>
              <option value="goal-blue">Blue</option>
              <option value="goal-red">Red</option>
            </select>
          </label>
          <label class="row"><span>Size (in)</span><input type="number" step="0.5" bind:value={one.w} onpointerdown={arm} oninput={() => (one.h = one.w)} onchange={disarm} /></label>
          <label class="row"><span>Rotation°</span><input type="number" step="1" bind:value={one.rotation} onpointerdown={arm} onchange={disarm} /></label>
        </div>
      {/if}
    {/if}
  {/if}
</div>

<style>
  .inspector {
    width: 258px;
    background: var(--panel);
    border-left: 1px solid var(--border);
    overflow-y: auto;
    padding: 10px 12px 40px;
    font-size: 12px;
  }
  .empty {
    color: var(--text-dim);
    margin-top: 30px;
    text-align: center;
  }
  .empty .dim {
    color: var(--text-faint);
    font-size: 11px;
  }
  .topbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding-bottom: 8px;
    border-bottom: 1px solid var(--border);
    margin-bottom: 8px;
  }
  .kind {
    text-transform: capitalize;
    font-weight: 600;
  }
  .acts button {
    background: var(--panel-2);
    border: 1px solid var(--border);
    color: var(--text-dim);
    border-radius: 5px;
    width: 26px;
    height: 24px;
    margin-left: 3px;
  }
  .acts button:hover {
    color: var(--text);
  }
  .acts .danger:hover {
    color: var(--danger);
    border-color: var(--danger);
  }
  .section {
    padding: 8px 0;
    border-bottom: 1px solid var(--border);
  }
  .hdr {
    font-weight: 600;
    margin-bottom: 6px;
  }
  .hdr.sub {
    margin-top: 10px;
    color: var(--text-dim);
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.05em;
  }
  .row {
    display: flex;
    align-items: center;
    gap: 8px;
    min-height: 26px;
  }
  .row > span:first-child {
    width: 82px;
    flex: none;
    color: var(--text-dim);
  }
  .row.checkbox > span {
    width: auto;
  }
  .row em {
    color: var(--text-faint);
    font-style: normal;
    width: 34px;
    text-align: right;
  }
  input[type='number'],
  input[type='text'],
  select,
  textarea {
    flex: 1;
    min-width: 0;
    background: var(--panel-2);
    border: 1px solid var(--border);
    color: var(--text);
    border-radius: 5px;
    padding: 4px 6px;
    font-family: inherit;
    font-size: 12px;
  }
  input[type='range'] {
    flex: 1;
    min-width: 0;
  }
  textarea {
    width: 100%;
    resize: vertical;
    margin-bottom: 6px;
  }
  .btnpair {
    display: flex;
    gap: 4px;
  }
  .btnpair button {
    background: var(--panel-2);
    border: 1px solid var(--border);
    color: var(--text-dim);
    border-radius: 5px;
    width: 28px;
    height: 24px;
  }
  .btnpair button.on {
    background: var(--accent-soft);
    border-color: var(--accent);
    color: var(--text);
  }
  button.wide {
    width: 100%;
    margin-top: 8px;
    background: var(--accent-soft);
    border: 1px solid var(--accent);
    color: var(--text);
    padding: 7px;
    border-radius: 6px;
  }
  button.wide:hover {
    background: var(--accent);
  }
  .mini {
    margin-top: 6px;
  }
</style>
