// Auton import log — a small persistent library of recently-imported traces.
// Each entry stores the raw trace text plus when it was added, so it can be
// re-loaded into the player from the Library panel.

import { sampleAutons } from '$lib/sample/sampleAutons';

export interface SavedAuton {
  id: string;
  name: string;
  importedAt: number; // ms since epoch
  text: string;
}

interface LibraryState {
  items: SavedAuton[];
  // ID of the auton currently loaded into the player, if any.
  activeId: string | null;
  // bump to force a reseed when the sample trace format changes
  seedVersion?: number;
}

const STORAGE_KEY = 'lucaslib-debugger.library';
const MAX_ITEMS = 25;
// Bump when the sample format changes (e.g. new govern fields) so stale seeded
// libraries refresh instead of showing samples missing the new columns.
const SEED_VERSION = 12;

function makeId(): string {
  return Math.random().toString(36).slice(2, 10) + Date.now().toString(36);
}

// Seed: pretend the samples were imported over the last few days so the list
// looks lived-in. Newest first.
function seedItems(): SavedAuton[] {
  const now = Date.now();
  // staggered: just now, 12m ago, 3h ago, 2d ago
  const offsets = [0, 12 * 60 * 1000, 3 * 60 * 60 * 1000, 2 * 24 * 60 * 60 * 1000];
  return sampleAutons.map((a, i) => ({
    id: makeId(),
    name: a.name,
    importedAt: now - (offsets[i] ?? offsets[offsets.length - 1] + i * 1000),
    text: a.text
  }));
}

function seeded(): LibraryState {
  const items = seedItems();
  return { items, activeId: items[0]?.id ?? null, seedVersion: SEED_VERSION };
}

function load(): LibraryState {
  if (typeof localStorage === 'undefined') {
    return seeded();
  }
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (raw) {
      const parsed = JSON.parse(raw) as LibraryState;
      // Reseed if the format version changed (drops stale samples).
      if (
        parsed &&
        Array.isArray(parsed.items) &&
        parsed.items.length > 0 &&
        parsed.seedVersion === SEED_VERSION
      ) {
        return {
          items: parsed.items,
          activeId: parsed.activeId ?? parsed.items[0].id,
          seedVersion: SEED_VERSION
        };
      }
    }
  } catch {
    // corrupt storage — reseed
  }
  return seeded();
}

export const library = $state<LibraryState>(load());

function persist() {
  if (typeof localStorage === 'undefined') return;
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(library));
  } catch {
    // quota exceeded or similar — ignore
  }
}

/**
 * Add a trace to the library. Returns the new SavedAuton.
 * If `text` matches the most-recent entry's text we don't double-record.
 */
export function addAuton(text: string, fallbackName = 'imported'): SavedAuton {
  // Cheap dedupe: same text as the head item -> just bump activeId
  if (library.items[0] && library.items[0].text === text) {
    library.activeId = library.items[0].id;
    persist();
    return library.items[0];
  }
  const name = extractAutonName(text) ?? fallbackName;
  const entry: SavedAuton = {
    id: makeId(),
    name,
    importedAt: Date.now(),
    text
  };
  library.items = [entry, ...library.items].slice(0, MAX_ITEMS);
  library.activeId = entry.id;
  persist();
  return entry;
}

export function setActive(id: string) {
  library.activeId = id;
  persist();
}

export function removeAuton(id: string) {
  library.items = library.items.filter((a) => a.id !== id);
  if (library.activeId === id) library.activeId = library.items[0]?.id ?? null;
  persist();
}

/** Find an entry by id. */
export function getAuton(id: string): SavedAuton | null {
  return library.items.find((a) => a.id === id) ?? null;
}

function extractAutonName(text: string): string | null {
  for (const line of text.split(/\r?\n/, 12)) {
    const m = line.match(/^#auton_name=(.+)$/);
    if (m) return m[1].trim();
  }
  return null;
}

/** Short stats about a trace, derived without a full parse. */
export function summarize(text: string): { movements: number; durationMs: number } {
  let inMovements = false;
  let inTicks = false;
  let movementCount = -1; // -1 to discount header row
  let lastTickT = 0;
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (line === '') continue;
    if (line === '[MOVEMENTS]') { inMovements = true; inTicks = false; continue; }
    if (line === '[TICKS]') { inMovements = false; inTicks = true; continue; }
    if (/^\[[A-Z]+\]$/.test(line)) { inMovements = false; inTicks = false; continue; }
    if (line.startsWith('#')) continue;
    if (inMovements) movementCount++;
    if (inTicks) {
      // first non-header tick row sets lastTickT; we keep updating as we go
      const firstCol = line.split(',', 1)[0];
      const n = Number(firstCol);
      if (!Number.isNaN(n)) lastTickT = n;
    }
  }
  return { movements: Math.max(0, movementCount), durationMs: lastTickT };
}

export function formatRelative(ms: number): string {
  const diff = Date.now() - ms;
  const s = Math.round(diff / 1000);
  if (s < 60) return 'just now';
  const m = Math.round(s / 60);
  if (m < 60) return `${m} min ago`;
  const h = Math.round(m / 60);
  if (h < 24) return `${h} hr ago`;
  const d = Math.round(h / 24);
  if (d < 7) return `${d} day${d === 1 ? '' : 's'} ago`;
  const date = new Date(ms);
  return date.toLocaleDateString(undefined, { month: 'short', day: 'numeric' });
}
