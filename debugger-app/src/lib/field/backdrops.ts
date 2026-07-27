// Top-down field renders used as the field-map backdrop. Each PNG is a square
// 12ft×12ft (= 144"×144") image, so it maps 1:1 onto the field SVG's
// 0 0 144 144 viewBox. "highlighted" variants show the auton path lines; "plain"
// variants show the game elements only. Files live in static/field/ and are
// named `<game>-<mode>[-highlighted].png`.

export type FieldBackdrop = string; // '<game>-<mode>' | '<game>-<mode>-h' | 'perimeter'

export interface GameSpec {
  key: string; // file prefix, e.g. 'override'
  label: string; // display name, e.g. 'Override'
}

// Newest season first. Add a game here + drop its four PNGs in static/field/.
export const GAMES: GameSpec[] = [
  { key: 'pushback', label: 'Push Back' },
  { key: 'highstakes', label: 'High Stakes' },
  { key: 'override', label: 'Override' }
];

export type FieldMode = 'match' | 'skills';
export const MODES: FieldMode[] = ['match', 'skills'];

export interface BackdropOption {
  id: FieldBackdrop;
  name: string;
  game?: string;
  mode?: FieldMode;
  variant?: 'highlighted' | 'plain';
  file: string;
}

function buildBackdrops(): BackdropOption[] {
  const out: BackdropOption[] = [{ id: 'perimeter', name: 'Empty field', file: 'empty.png' }];
  for (const g of GAMES) {
    for (const mode of MODES) {
      const modeLabel = mode === 'match' ? 'Match' : 'Skills';
      out.push({
        id: `${g.key}-${mode}-h`,
        name: `${g.label} ${modeLabel}`,
        game: g.key,
        mode,
        variant: 'highlighted',
        file: `${g.key}-${mode}-highlighted.png`
      });
      out.push({
        id: `${g.key}-${mode}`,
        name: `${g.label} ${modeLabel}`,
        game: g.key,
        mode,
        variant: 'plain',
        file: `${g.key}-${mode}.png`
      });
    }
  }
  return out;
}

export const BACKDROPS: BackdropOption[] = buildBackdrops();

// Old flat ids (Override-only era) → new game-prefixed ids, for migrating
// persisted settings written before the multi-game rename.
export const LEGACY_BACKDROP_IDS: Record<string, FieldBackdrop> = {
  'match-h': 'override-match-h',
  match: 'override-match',
  'skills-h': 'override-skills-h',
  skills: 'override-skills'
};

export function normalizeBackdropId(id: string | undefined | null): FieldBackdrop {
  if (!id) return 'perimeter';
  if (LEGACY_BACKDROP_IDS[id]) return LEGACY_BACKDROP_IDS[id];
  return BACKDROPS.some((b) => b.id === id) ? id : 'perimeter';
}

export function findBackdrop(id: FieldBackdrop): BackdropOption {
  return BACKDROPS.find((b) => b.id === normalizeBackdropId(id)) ?? BACKDROPS[0];
}

// The 144"×144" coordinate space is the TILE grid, NOT the inner wall face: the
// outer tiles' interlocking nubs run *under* the perimeter wall, so the playable
// surface extends past the visible wall. Measured from the renders' tile-shade
// seams (a 6×6 checkerboard centered on the image), the tile grid spans image
// px [53.6, 3946.4] of 4000 — a 1.34% inset per side, pitch ≈ 648.8 px, centered
// on the image — giving exactly 6 × 24".
export const FIELD_BORDER_FRAC = 0.0134;

/** Placement for the backdrop <image> so the tile area maps onto [0, size]. */
export function fieldImageRect(size: number): { x: number; y: number; w: number; h: number } {
  const play = 1 - 2 * FIELD_BORDER_FRAC;
  const render = size / play;
  const offset = -size * (FIELD_BORDER_FRAC / play);
  return { x: offset, y: offset, w: render, h: render };
}

/** Public URL for a backdrop, respecting the app base path. */
export function backdropSrc(id: FieldBackdrop, base = ''): string {
  return `${base}/field/${encodeURIComponent(findBackdrop(id).file)}`;
}
