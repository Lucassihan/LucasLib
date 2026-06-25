// Top-down field renders used as the field-map backdrop. Each PNG is a square
// 12ft×12ft (= 144"×144") image, so it maps 1:1 onto the field SVG's
// 0 0 144 144 viewBox. "highlighted" variants show the auton path lines; "plain"
// variants show the game elements only. Files live in static/field/.

export type FieldBackdrop = 'perimeter' | 'match-h' | 'match' | 'skills-h' | 'skills';

export interface BackdropOption {
  id: FieldBackdrop;
  name: string;
  variant?: 'highlighted' | 'plain';
  file: string;
}

export const BACKDROPS: BackdropOption[] = [
  { id: 'perimeter', name: 'Empty field', file: 'empty.png' },
  { id: 'match-h', name: 'Override Match', variant: 'highlighted', file: 'match-highlighted.png' },
  { id: 'match', name: 'Override Match', variant: 'plain', file: 'match.png' },
  { id: 'skills-h', name: 'Override Skills', variant: 'highlighted', file: 'skills-highlighted.png' },
  { id: 'skills', name: 'Override Skills', variant: 'plain', file: 'skills.png' }
];

export function findBackdrop(id: FieldBackdrop): BackdropOption {
  return BACKDROPS.find((b) => b.id === id) ?? BACKDROPS[0];
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
