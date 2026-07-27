// Curated font choices — all are in the Google Docs font menu AND installed on
// macOS, so they render identically on the canvas and in the exported PNG (the
// standalone SVG rasterizer uses the same system fonts; no embedding needed).

export interface FontDef {
  name: string;
  stack: string;
}

export const FONTS: FontDef[] = [
  { name: 'Arial', stack: 'Arial, Helvetica, sans-serif' },
  { name: 'Helvetica', stack: '"Helvetica Neue", Helvetica, Arial, sans-serif' },
  { name: 'Verdana', stack: 'Verdana, Geneva, sans-serif' },
  { name: 'Trebuchet MS', stack: '"Trebuchet MS", Tahoma, sans-serif' },
  { name: 'Times New Roman', stack: '"Times New Roman", Times, serif' },
  { name: 'Georgia', stack: 'Georgia, "Times New Roman", serif' },
  { name: 'Sitka Text', stack: '"Sitka Text", Cambria, Georgia, serif' },
  { name: 'Courier New', stack: '"Courier New", Courier, monospace' },
  { name: 'Comic Sans MS', stack: '"Comic Sans MS", "Comic Sans", cursive' }
];

export const DEFAULT_FONT = 'Arial';

export function fontStack(name: string): string {
  return FONTS.find((f) => f.name === name)?.stack ?? FONTS[0].stack;
}
