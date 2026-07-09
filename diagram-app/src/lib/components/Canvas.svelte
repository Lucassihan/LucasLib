<script lang="ts">
  import {
    doc,
    ui,
    snapshot,
    selectOnly,
    toggleSelect,
    clearSelection,
    isSelected,
    addElement
  } from '$lib/model/scene.svelte';
  import type { Element, Pt, RobotEl, PathEl, ShapeEl, TextEl, StampEl } from '$lib/model/types';
  import { holesToInches } from '$lib/geom/chassis';
  import { getHandles, applyHandle } from '$lib/canvas/handles';
  import { collectSnapPoints, snapField } from '$lib/canvas/snap';
  import { createRobot, createPath, createShape, createText, createStamp } from '$lib/model/defaults';
  import { fontStack } from '$lib/model/fonts';
  import fieldEmpty from '$lib/assets/field-empty.png';

  import RobotElement from './RobotElement.svelte';
  import PathShape from './PathShape.svelte';
  import ShapeShape from './ShapeShape.svelte';
  import TextShape from './TextShape.svelte';
  import StampShape from './StampShape.svelte';

  let wrap: HTMLDivElement;
  let svgEl: SVGSVGElement;
  let w = $state(800);
  let h = $state(600);

  // field render mapping (playable 144" area inset 1.34% inside the image)
  const B = 0.0134;
  const FULL = 144 / (1 - 2 * B);
  const HALF = FULL / 2;

  const ticks = [-72, -48, -24, 0, 24, 48, 72];

  const selId = $derived(ui.selectedIds.length === 1 ? ui.selectedIds[0] : null);
  const selEl = $derived(selId ? doc.elements.find((e) => e.id === selId) ?? null : null);
  const handles = $derived(selEl ? getHandles(selEl) : []);

  let snapPt = $state<Pt | null>(null);

  // ---- coordinate conversion ---------------------------------------------
  function toField(clientX: number, clientY: number): Pt {
    const r = svgEl.getBoundingClientRect();
    const wx = (clientX - r.left - ui.view.tx) / ui.view.s;
    const wy = (clientY - r.top - ui.view.ty) / ui.view.s;
    return { x: wx, y: -wy };
  }

  // ---- drag state ---------------------------------------------------------
  type Drag =
    | { mode: 'pan'; sx: number; sy: number; tx: number; ty: number }
    | { mode: 'move'; start: Pt; orig: Map<string, string>; moved: boolean }
    | { mode: 'handle'; elId: string; handleId: string }
    | { mode: 'createShape'; elId: string; anchor: Pt }
    | { mode: 'createPath'; elId: string; anchor: Pt; kind: PathEl['kind'] };
  let drag: Drag | null = null;

  function geomClone(el: Element): string {
    return JSON.stringify(el);
  }

  function refPoint(el: Element): Pt {
    if (el.type === 'path') return el.points[0];
    return { x: (el as any).x, y: (el as any).y };
  }

  // ---- pointer handlers ---------------------------------------------------
  function onPointerDown(e: PointerEvent) {
    if (e.button === 2) return;
    try {
      svgEl.setPointerCapture(e.pointerId);
    } catch {
      /* synthetic pointers have no capture */
    }

    if (e.button === 1) {
      drag = { mode: 'pan', sx: e.clientX, sy: e.clientY, tx: ui.view.tx, ty: ui.view.ty };
      return;
    }

    const target = e.target as SVGElement;
    const handleNode = target.closest('[data-handle]');
    if (handleNode && selId) {
      snapshot();
      drag = { mode: 'handle', elId: selId, handleId: handleNode.getAttribute('data-handle')! };
      return;
    }

    const field = toField(e.clientX, e.clientY);

    if (ui.tool !== 'select') {
      createAt(field, e);
      return;
    }

    const elNode = target.closest('[data-el]');
    if (elNode) {
      const id = elNode.getAttribute('data-el')!;
      if (e.shiftKey) toggleSelect(id);
      else if (!isSelected(id)) selectOnly(id);
      beginMove(field);
    } else {
      if (!e.shiftKey) clearSelection();
      drag = { mode: 'pan', sx: e.clientX, sy: e.clientY, tx: ui.view.tx, ty: ui.view.ty };
    }
  }

  function beginMove(field: Pt) {
    snapshot();
    const orig = new Map<string, string>();
    for (const el of doc.elements) if (isSelected(el.id)) orig.set(el.id, geomClone(el));
    drag = { mode: 'move', start: field, orig, moved: false };
  }

  function createAt(field: Pt, e: PointerEvent) {
    const t = ui.tool;
    if (t === 'robot') {
      const el = createRobot(field.x, field.y);
      addElement(el);
      beginMove(field);
    } else if (t === 'text') {
      addElement(createText(field.x, field.y));
    } else if (t === 'stamp-goal-blue' || t === 'stamp-goal-red') {
      const kind = t === 'stamp-goal-blue' ? 'goal-blue' : 'goal-red';
      addElement(createStamp(kind, field.x, field.y));
    } else if (t === 'circle' || t === 'rect') {
      const el = createShape(t === 'circle' ? 'circle' : 'rect', field.x, field.y, 2, 2);
      addElement(el);
      drag = { mode: 'createShape', elId: el.id, anchor: field };
    } else {
      const kind = t as PathEl['kind'];
      const el = createPath(kind, field, { x: field.x + 0.01, y: field.y });
      addElement(el);
      drag = { mode: 'createPath', elId: el.id, anchor: field, kind };
    }
  }

  function onPointerMove(e: PointerEvent) {
    if (!drag) return;
    if (drag.mode === 'pan') {
      ui.view.tx = drag.tx + (e.clientX - drag.sx);
      ui.view.ty = drag.ty + (e.clientY - drag.sy);
      return;
    }
    const field = toField(e.clientX, e.clientY);

    if (drag.mode === 'move') {
      let dx = field.x - drag.start.x;
      let dy = field.y - drag.start.y;
      // snap using the primary element's reference point
      snapPt = null;
      if (ui.snap) {
        const primaryId = ui.selectedIds[0];
        const origStr = drag.orig.get(primaryId);
        if (origStr) {
          const origEl = JSON.parse(origStr) as Element;
          const ref = refPoint(origEl);
          const moved = { x: ref.x + dx, y: ref.y + dy };
          const cands = collectSnapPoints(doc.elements, new Set(ui.selectedIds));
          const snapped = snapField(moved, cands, ui.view.s);
          if (snapped !== moved) {
            dx += snapped.x - moved.x;
            dy += snapped.y - moved.y;
            snapPt = snapped;
          }
        }
      }
      for (const el of doc.elements) {
        const s = drag.orig.get(el.id);
        if (!s) continue;
        const o = JSON.parse(s) as Element;
        if (el.type === 'path' && o.type === 'path') {
          el.points = o.points.map((p) => ({ x: p.x + dx, y: p.y + dy }));
        } else {
          (el as any).x = (o as any).x + dx;
          (el as any).y = (o as any).y + dy;
        }
      }
      drag.moved = true;
      return;
    }

    if (drag.mode === 'handle') {
      const el = doc.elements.find((x) => x.id === drag!.elId);
      if (!el) return;
      let p = field;
      snapPt = null;
      const hid = (drag as any).handleId as string;
      const isRotate = hid.startsWith('h') || hid === 'rot';
      if (ui.snap && !isRotate) {
        const cands = collectSnapPoints(doc.elements, new Set([el.id]));
        const snapped = snapField(field, cands, ui.view.s);
        if (snapped !== field) {
          p = snapped;
          snapPt = snapped;
        }
      }
      applyHandle(el, hid, p.x, p.y);
      return;
    }

    if (drag.mode === 'createShape') {
      const el = doc.elements.find((x) => x.id === drag!.elId) as ShapeEl | undefined;
      if (!el) return;
      const a = drag.anchor;
      el.x = (a.x + field.x) / 2;
      el.y = (a.y + field.y) / 2;
      if (el.shape === 'circle') {
        el.w = el.h = Math.max(1, Math.hypot(field.x - a.x, field.y - a.y));
      } else {
        el.w = Math.max(1, Math.abs(field.x - a.x));
        el.h = Math.max(1, Math.abs(field.y - a.y));
      }
      return;
    }

    if (drag.mode === 'createPath') {
      const el = doc.elements.find((x) => x.id === drag!.elId) as PathEl | undefined;
      if (!el) return;
      const fresh = createPath(drag.kind, drag.anchor, field);
      el.points = fresh.points;
      if (el.kind === 'splineHeading') {
        el.headingStart = fresh.headingStart;
        el.headingEnd = fresh.headingEnd;
      }
      if (el.kind === 'boomerang' && el.boom && fresh.boom) {
        el.boom.startHeading = fresh.boom.startHeading;
        el.boom.targetHeading = fresh.boom.targetHeading;
      }
      return;
    }
  }

  function onPointerUp(e: PointerEvent) {
    if (drag && (drag.mode === 'createShape' || drag.mode === 'createPath')) {
      finalizeCreate(drag);
    }
    drag = null;
    snapPt = null;
  }

  function finalizeCreate(d: Drag & { mode: 'createShape' | 'createPath' }) {
    const el = doc.elements.find((x) => x.id === d.elId);
    if (!el) return;
    if (el.type === 'shape' && el.w < 2 && el.h < 2) {
      el.w = el.h = 10;
    }
    if (el.type === 'path') {
      const a = el.points[0];
      const b = el.points[el.points.length - 1];
      if (Math.hypot(b.x - a.x, b.y - a.y) < 2) {
        const fresh = createPath(el.kind, a, { x: a.x + 24, y: a.y });
        el.points = fresh.points;
        if (el.kind === 'splineHeading') el.headingStart = fresh.headingStart;
        if (el.kind === 'boomerang' && el.boom && fresh.boom) {
          el.boom.startHeading = fresh.boom.startHeading;
          el.boom.targetHeading = fresh.boom.targetHeading;
        }
      }
    }
  }

  function onWheel(e: WheelEvent) {
    e.preventDefault();
    if (e.ctrlKey || e.metaKey) {
      const r = svgEl.getBoundingClientRect();
      const mx = e.clientX - r.left;
      const my = e.clientY - r.top;
      const factor = Math.exp(-e.deltaY * 0.01);
      const news = Math.min(60, Math.max(0.4, ui.view.s * factor));
      ui.view.tx = mx - (mx - ui.view.tx) * (news / ui.view.s);
      ui.view.ty = my - (my - ui.view.ty) * (news / ui.view.s);
      ui.view.s = news;
    } else {
      ui.view.tx -= e.deltaX;
      ui.view.ty -= e.deltaY;
    }
  }

  // center the field on first mount if view is at origin default
  $effect(() => {
    if (ui.view.tx === 0 && ui.view.ty === 0 && w > 0) {
      ui.view.tx = w / 2;
      ui.view.ty = h / 2;
    }
  });

  // ---- selection bbox (svg-world rect) ------------------------------------
  function bbox(el: Element): { x: number; y: number; w: number; h: number } {
    let minx = 0,
      maxx = 0,
      miny = 0,
      maxy = 0;
    if (el.type === 'path') {
      const xs = el.points.map((p) => p.x);
      const ys = el.points.map((p) => p.y);
      minx = Math.min(...xs) - 2;
      maxx = Math.max(...xs) + 2;
      miny = Math.min(...ys) - 2;
      maxy = Math.max(...ys) + 2;
    } else if (el.type === 'robot') {
      const r = Math.hypot(holesToInches(el.chassis.widthHoles), holesToInches(el.chassis.lengthHoles)) / 2 + 1.5;
      minx = el.x - r;
      maxx = el.x + r;
      miny = el.y - r;
      maxy = el.y + r;
    } else if (el.type === 'stamp') {
      const r = Math.hypot(el.w, el.h) / 2;
      minx = el.x - r;
      maxx = el.x + r;
      miny = el.y - r;
      maxy = el.y + r;
    } else if (el.type === 'shape') {
      minx = el.x - el.w / 2;
      maxx = el.x + el.w / 2;
      miny = el.y - el.h / 2;
      maxy = el.y + el.h / 2;
    } else {
      const bw = Math.max(1, ...el.text.split('\n').map((l) => l.length)) * el.size * 0.58;
      const bh = el.text.split('\n').length * el.size * 1.25;
      minx = el.align === 'center' ? el.x - bw / 2 : el.align === 'right' ? el.x - bw : el.x;
      maxx = minx + bw;
      miny = el.y - el.size;
      maxy = miny + bh;
    }
    return { x: minx, y: -maxy, w: maxx - minx, h: maxy - miny };
  }
</script>

<div
  class="wrap"
  class:paper={ui.backdrop === 'paper'}
  class:dark={ui.backdrop === 'dark'}
  class:checker={ui.backdrop === 'checker'}
  bind:this={wrap}
  bind:clientWidth={w}
  bind:clientHeight={h}
>
  <svg
    id="diagram-canvas"
    bind:this={svgEl}
    width={w}
    height={h}
    onpointerdown={onPointerDown}
    onpointermove={onPointerMove}
    onpointerup={onPointerUp}
    onwheel={onWheel}
    oncontextmenu={(e) => e.preventDefault()}
    class:tool-active={ui.tool !== 'select'}
  >
    <g transform="translate({ui.view.tx},{ui.view.ty}) scale({ui.view.s})" font-family={fontStack(ui.font)}>
      {#if ui.showField}
        <image class="field-bg" href={fieldEmpty} x={-HALF} y={-HALF} width={FULL} height={FULL} opacity="0.9" style="pointer-events:none" />
      {/if}

      {#if ui.showGrid}
        <g class="grid" style="pointer-events:none">
          {#each ticks as t}
            <line x1={t} y1={-72} x2={t} y2={72} vector-effect="non-scaling-stroke" class:axis={t === 0} />
            <line x1={-72} y1={t} x2={72} y2={t} vector-effect="non-scaling-stroke" class:axis={t === 0} />
          {/each}
          <rect x={-72} y={-72} width={144} height={144} fill="none" class="border" vector-effect="non-scaling-stroke" />
        </g>
      {/if}

      {#each doc.elements as el (el.id)}
        {#if el.type === 'robot'}
          <RobotElement el={el as RobotEl} />
        {:else if el.type === 'path'}
          <PathShape el={el as PathEl} scale={ui.view.s} />
        {:else if el.type === 'shape'}
          <ShapeShape el={el as ShapeEl} />
        {:else if el.type === 'text'}
          <TextShape el={el as TextEl} />
        {:else if el.type === 'stamp'}
          <StampShape el={el as StampEl} />
        {/if}
      {/each}

      <!-- selection outlines -->
      {#each doc.elements as el (el.id)}
        {#if isSelected(el.id)}
          {@const b = bbox(el)}
          <rect
            x={b.x}
            y={b.y}
            width={b.w}
            height={b.h}
            fill="none"
            class="selbox"
            vector-effect="non-scaling-stroke"
            style="pointer-events:none"
          />
        {/if}
      {/each}

      <!-- handles for single selection -->
      {#if selEl}
        {#each handles as hnd}
          {#if hnd.kind === 'rotate'}
            <circle
              cx={hnd.x}
              cy={-hnd.y}
              r={4.5 / ui.view.s}
              class="handle rot"
              data-handle={hnd.id}
              vector-effect="non-scaling-stroke"
            />
          {:else}
            <rect
              x={hnd.x - 4 / ui.view.s}
              y={-hnd.y - 4 / ui.view.s}
              width={8 / ui.view.s}
              height={8 / ui.view.s}
              class="handle"
              data-handle={hnd.id}
              vector-effect="non-scaling-stroke"
            />
          {/if}
        {/each}
      {/if}

      {#if snapPt}
        <circle cx={snapPt.x} cy={-snapPt.y} r={6 / ui.view.s} class="snap" vector-effect="non-scaling-stroke" />
      {/if}
    </g>
  </svg>
</div>

<style>
  .wrap {
    position: relative;
    width: 100%;
    height: 100%;
    overflow: hidden;
  }
  .paper {
    background: #f4f5f3;
  }
  .dark {
    background: #12161c;
  }
  .checker {
    background-color: #cfd3d6;
    background-image:
      linear-gradient(45deg, #a9aeb2 25%, transparent 25%),
      linear-gradient(-45deg, #a9aeb2 25%, transparent 25%),
      linear-gradient(45deg, transparent 75%, #a9aeb2 75%),
      linear-gradient(-45deg, transparent 75%, #a9aeb2 75%);
    background-size: 18px 18px;
    background-position:
      0 0,
      0 9px,
      9px -9px,
      -9px 0;
  }
  svg {
    display: block;
    touch-action: none;
  }
  svg.tool-active {
    cursor: crosshair;
  }
  .grid line {
    stroke: rgba(110, 120, 130, 0.28);
    stroke-width: 1;
  }
  .grid line.axis {
    stroke: rgba(110, 120, 130, 0.5);
  }
  .grid .border {
    stroke: rgba(110, 120, 130, 0.6);
    stroke-width: 1.5;
  }
  .dark .grid line {
    stroke: rgba(150, 165, 180, 0.16);
  }
  .dark .grid line.axis {
    stroke: rgba(150, 165, 180, 0.3);
  }
  .dark .grid .border {
    stroke: rgba(150, 165, 180, 0.32);
  }
  .selbox {
    stroke: var(--accent);
    stroke-width: 1.2;
    stroke-dasharray: 4 3;
    opacity: 0.9;
  }
  .handle {
    fill: #ffffff;
    stroke: var(--accent);
    stroke-width: 1.5;
    cursor: pointer;
  }
  .handle.rot {
    fill: var(--accent);
    stroke: #ffffff;
  }
  .snap {
    fill: none;
    stroke: #f59e0b;
    stroke-width: 1.5;
  }
</style>
