// Reactive scene store (Svelte 5 runes): the element list, selection, view
// transform, tool, snapshot-based undo/redo, a named-designs library, and
// localStorage autosave + save/open to a .json file.

import type { Element, PathEl, RobotChassis } from './types';
import { samplePath, poseAtT } from '$lib/geom/paths';
import { createRobot, uid } from './defaults';
import { DEFAULT_FONT } from './fonts';

export type Tool =
  | 'select'
  | 'robot'
  | 'straight'
  | 'arc'
  | 'spline'
  | 'splineHeading'
  | 'boomerang'
  | 'circle'
  | 'rect'
  | 'text'
  | 'stamp-goal-blue'
  | 'stamp-goal-red';

export type Backdrop = 'paper' | 'dark' | 'checker';
export type FadeMode = 'none' | 'end' | 'start';

const MAIN_KEY = 'lucaslib-diagram.v2';
const OLD_KEY = 'lucaslib-diagram.v1';
const docKey = (id: string) => `lucaslib-diagram.doc.${id}`;

interface DiagramMeta {
  id: string;
  name: string;
}

function lsGet(k: string): string | null {
  try {
    return typeof localStorage !== 'undefined' ? localStorage.getItem(k) : null;
  } catch {
    return null;
  }
}
function lsSet(k: string, v: string): void {
  try {
    localStorage.setItem(k, v);
  } catch {
    /* file:// / private mode — durable save is via saveToFile */
  }
}
function lsDel(k: string): void {
  try {
    localStorage.removeItem(k);
  } catch {
    /* ignore */
  }
}

function loadDocElements(id: string): Element[] {
  const raw = lsGet(docKey(id));
  if (raw) {
    try {
      const p = JSON.parse(raw);
      return Array.isArray(p) ? p : (p.elements ?? []);
    } catch {
      /* ignore */
    }
  }
  return [];
}

// ---- initial load (with one-time migration from the old single-doc key) ----
interface MainPersist {
  diagrams: DiagramMeta[];
  activeId: string;
  ui?: {
    backdrop?: Backdrop;
    showGrid?: boolean;
    showField?: boolean;
    snap?: boolean;
    font?: string;
    view?: { tx: number; ty: number; s: number };
  };
}

function initialMain(): { main: MainPersist; elements: Element[] } {
  const raw = lsGet(MAIN_KEY);
  if (raw) {
    try {
      const main = JSON.parse(raw) as MainPersist;
      if (main.diagrams?.length) return { main, elements: loadDocElements(main.activeId) };
    } catch {
      /* ignore */
    }
  }
  // fresh, or migrate a v1 doc into a first design
  const id = uid();
  let elements: Element[] = [];
  let ui0: MainPersist['ui'];
  const old = lsGet(OLD_KEY);
  if (old) {
    try {
      const p = JSON.parse(old);
      elements = p.elements ?? [];
      ui0 = { ...(p.ui ?? {}), view: p.view };
    } catch {
      /* ignore */
    }
  }
  const main: MainPersist = { diagrams: [{ id, name: 'Untitled' }], activeId: id, ui: ui0 };
  lsSet(docKey(id), JSON.stringify(elements));
  lsSet(MAIN_KEY, JSON.stringify(main));
  return { main, elements };
}

const boot = initialMain();

export const library = $state<{ diagrams: DiagramMeta[]; activeId: string }>({
  diagrams: boot.main.diagrams,
  activeId: boot.main.activeId
});

export const doc = $state<{ elements: Element[] }>({ elements: boot.elements });

export const ui = $state({
  tool: 'select' as Tool,
  selectedIds: [] as string[],
  view: boot.main.ui?.view ?? { tx: 0, ty: 0, s: 4 }, // s = px per inch
  backdrop: (boot.main.ui?.backdrop ?? 'paper') as Backdrop,
  showGrid: boot.main.ui?.showGrid ?? true,
  showField: boot.main.ui?.showField ?? false,
  snap: boot.main.ui?.snap ?? true,
  font: boot.main.ui?.font ?? DEFAULT_FONT
});

// ---- history (snapshot based, per-design) --------------------------------
let past: string[] = [];
let future: string[] = [];

function resetHistory(): void {
  past = [];
  future = [];
}

export function snapshot(): void {
  past.push(JSON.stringify(doc.elements));
  if (past.length > 120) past.shift();
  future = [];
}

export function undo(): void {
  if (!past.length) return;
  future.push(JSON.stringify(doc.elements));
  doc.elements = JSON.parse(past.pop()!);
  pruneSelection();
}

export function redo(): void {
  if (!future.length) return;
  past.push(JSON.stringify(doc.elements));
  doc.elements = JSON.parse(future.pop()!);
  pruneSelection();
}

function pruneSelection(): void {
  ui.selectedIds = ui.selectedIds.filter((id) => doc.elements.some((e) => e.id === id));
}

// ---- selection ------------------------------------------------------------
export function selectOnly(id: string | null): void {
  ui.selectedIds = id ? [id] : [];
}
export function toggleSelect(id: string): void {
  ui.selectedIds = ui.selectedIds.includes(id)
    ? ui.selectedIds.filter((i) => i !== id)
    : [...ui.selectedIds, id];
}
export function clearSelection(): void {
  ui.selectedIds = [];
}
export function isSelected(id: string): boolean {
  return ui.selectedIds.includes(id);
}
export function selectedElements(): Element[] {
  return doc.elements.filter((e) => ui.selectedIds.includes(e.id));
}
export function getEl(id: string): Element | undefined {
  return doc.elements.find((e) => e.id === id);
}

// ---- mutations ------------------------------------------------------------
export function addElement(el: Element, select = true): void {
  snapshot();
  doc.elements.push(el);
  if (select) selectOnly(el.id);
}

export function deleteSelected(): void {
  if (!ui.selectedIds.length) return;
  snapshot();
  doc.elements = doc.elements.filter((e) => !ui.selectedIds.includes(e.id));
  ui.selectedIds = [];
}

export function duplicateSelected(): void {
  const sel = selectedElements();
  if (!sel.length) return;
  snapshot();
  const clones = sel.map((e) => {
    const c = JSON.parse(JSON.stringify(e)) as Element;
    c.id = uid();
    if ('x' in c && 'y' in c) {
      (c as any).x += 4;
      (c as any).y -= 4;
    }
    if (c.type === 'path') c.points = c.points.map((p) => ({ x: p.x + 4, y: p.y - 4 }));
    return c;
  });
  doc.elements.push(...clones);
  ui.selectedIds = clones.map((c) => c.id);
}

export function bringToFront(): void {
  const sel = selectedElements();
  if (!sel.length) return;
  snapshot();
  doc.elements = [...doc.elements.filter((e) => !ui.selectedIds.includes(e.id)), ...sel];
}
export function sendToBack(): void {
  const sel = selectedElements();
  if (!sel.length) return;
  snapshot();
  doc.elements = [...sel, ...doc.elements.filter((e) => !ui.selectedIds.includes(e.id))];
}

export function clearAll(): void {
  snapshot();
  doc.elements = [];
  ui.selectedIds = [];
}

export function loadElements(els: Element[]): void {
  snapshot();
  doc.elements = els;
  ui.selectedIds = [];
}

/** Drop N robot copies evenly along a path; `fade` sets which end stays bright. */
export function addGhostsAlong(
  path: PathEl,
  n: number,
  fade: FadeMode,
  chassis: RobotChassis,
  color: string
): void {
  const sp = samplePath(path);
  snapshot();
  const created: string[] = [];
  for (let i = 0; i < n; i++) {
    const f = n <= 1 ? 0 : i / (n - 1);
    const pose = poseAtT(sp, f);
    const robot = createRobot(pose.x, pose.y, pose.heading);
    robot.chassis = JSON.parse(JSON.stringify(chassis));
    robot.color = color;
    robot.fill = true;
    if (fade === 'end') robot.opacity = 1 - f * 0.72; // bright at start
    else if (fade === 'start') robot.opacity = 1 - (1 - f) * 0.72; // bright at end
    else robot.opacity = 1;
    doc.elements.push(robot);
    created.push(robot.id);
  }
  ui.selectedIds = created;
}

// ---- designs library ------------------------------------------------------
export function activeMeta(): DiagramMeta {
  return library.diagrams.find((d) => d.id === library.activeId) ?? library.diagrams[0];
}

function persistCurrent(): void {
  lsSet(docKey(library.activeId), JSON.stringify(doc.elements));
}

function uniqueName(base: string): string {
  const names = new Set(library.diagrams.map((d) => d.name));
  if (!names.has(base)) return base;
  let i = 2;
  while (names.has(`${base} ${i}`)) i++;
  return `${base} ${i}`;
}

export function newDiagram(name = 'Untitled'): void {
  persistCurrent();
  const id = uid();
  library.diagrams.push({ id, name: uniqueName(name) });
  library.activeId = id;
  doc.elements = [];
  ui.selectedIds = [];
  resetHistory();
}

export function switchDiagram(id: string): void {
  if (id === library.activeId) return;
  persistCurrent();
  library.activeId = id;
  doc.elements = loadDocElements(id);
  ui.selectedIds = [];
  resetHistory();
}

export function renameDiagram(id: string, name: string): void {
  const m = library.diagrams.find((d) => d.id === id);
  if (m && name.trim()) m.name = name.trim();
}

export function duplicateDiagram(id: string): void {
  persistCurrent();
  const src = library.diagrams.find((d) => d.id === id);
  if (!src) return;
  const els = id === library.activeId ? doc.elements : loadDocElements(id);
  const nid = uid();
  library.diagrams.push({ id: nid, name: uniqueName(`${src.name} copy`) });
  const cloned = JSON.parse(JSON.stringify(els));
  lsSet(docKey(nid), JSON.stringify(cloned));
  library.activeId = nid;
  doc.elements = cloned;
  ui.selectedIds = [];
  resetHistory();
}

export function deleteDiagram(id: string): void {
  const idx = library.diagrams.findIndex((d) => d.id === id);
  if (idx < 0) return;
  library.diagrams.splice(idx, 1);
  lsDel(docKey(id));
  if (library.diagrams.length === 0) {
    const nid = uid();
    library.diagrams.push({ id: nid, name: 'Untitled' });
    library.activeId = nid;
    doc.elements = [];
  } else if (library.activeId === id) {
    library.activeId = library.diagrams[Math.max(0, idx - 1)].id;
    doc.elements = loadDocElements(library.activeId);
  }
  ui.selectedIds = [];
  resetHistory();
}

// ---- file save / open -----------------------------------------------------
function safeName(name: string): string {
  return name.replace(/[^\w.-]+/g, '_').replace(/^_+|_+$/g, '') || 'diagram';
}

export function saveToFile(): void {
  const meta = activeMeta();
  const data = {
    app: 'lucaslib-diagram',
    version: 2,
    name: meta.name,
    font: ui.font,
    elements: doc.elements,
    view: ui.view
  };
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `${safeName(meta.name)}.diagram.json`;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export async function openFromFile(file: File): Promise<boolean> {
  let data: any;
  try {
    data = JSON.parse(await file.text());
  } catch {
    return false;
  }
  const els: Element[] | null = Array.isArray(data) ? data : (data.elements ?? null);
  if (!els) return false;
  persistCurrent();
  const id = uid();
  const name = (data.name || file.name.replace(/\.(diagram\.)?json$/i, '')) || 'Imported';
  library.diagrams.push({ id, name: uniqueName(name) });
  const cloned = JSON.parse(JSON.stringify(els));
  lsSet(docKey(id), JSON.stringify(cloned));
  library.activeId = id;
  doc.elements = cloned;
  ui.selectedIds = [];
  if (data.view) ui.view = data.view;
  if (data.font) ui.font = data.font;
  resetHistory();
  return true;
}

// dev-only inspection hook for automated verification
if (typeof window !== 'undefined' && import.meta.env?.DEV) {
  (window as any).__scene = { doc, ui, library };
}

// ---- autosave -------------------------------------------------------------
$effect.root(() => {
  $effect(() => {
    lsSet(docKey(library.activeId), JSON.stringify(doc.elements));
    lsSet(
      MAIN_KEY,
      JSON.stringify({
        diagrams: library.diagrams,
        activeId: library.activeId,
        ui: {
          backdrop: ui.backdrop,
          showGrid: ui.showGrid,
          showField: ui.showField,
          snap: ui.snap,
          font: ui.font,
          view: ui.view
        }
      })
    );
  });
});
