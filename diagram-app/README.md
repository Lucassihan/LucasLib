# Notebook Diagram Maker

A local web app for quickly making **static robot/movement diagrams** for the code
section of the engineering notebook. Vector canvas tuned for VEX: draws the same
chassis as the LucasLib debugger, in the same field coordinates (heading 0 = +Y
up, CW positive), and exports **transparent PNGs** ready to drop into the notebook.

## Run

**Dev server** (hot reload):

```bash
npm install
npm run dev        # http://localhost:5180
```

**Standalone single file** (double-click, no server — like the other notebook tools):

```bash
npm run build      # → dist/index.html  (one self-contained file, ~550 KB)
```

`dist/index.html` inlines all JS/CSS/images and uses a relative base, so you can
open it straight from `file://` or move/rename it anywhere. Do NOT open the
project's root `index.html` directly — that's the dev shell and needs Vite.

(Both registered in `.claude/launch.json` as `diagram-app` and `diagram-dist`.)

## What it does

- **Robots** — place as many as you want; each has the debugger's chassis settings
  (width/length in holes, 4/6/8 drive wheels, 6-wheel layout, intake side,
  tracking center), color, body tint, heading, and an optional label. No center
  heading line — the intake edge shows facing.
- **Lines** — straight · arc-of-a-circle · spline (add/remove control points) ·
  spline with a fixed start (and optional end) heading · **boomerang**. Per line:
  color, width, dash (solid/dashed/dotted), arrowheads (none/start/end/both).
- **Boomerang** — a kinematic port of `boomerangTo` (three-stage carrot pursuit,
  lead as a coefficient of the initial distance). Toggle the **lead axis +
  carrot/ghost anchors**, and drop a **robot onto the path** with a scrub slider
  to screengrab the move at any moment.
- **Ghost robots along a path** — drop N robot copies evenly along any line
  (headings follow the curve) to show motion in a single static image; fade
  **bright-at-start**, **bright-at-end**, or no fade.
- **Shapes** — circles and rectangles, filled/hollow, dashed, any color.
- **Text** — bold/italic, size, color, alignment; move anywhere. A global **Font**
  picker (header) sets the font for all text/labels — web-safe Google-Docs fonts
  (Arial, Times New Roman, Georgia, Verdana, Trebuchet, Courier, Comic Sans) that
  render identically on the canvas and in the exported PNG.
- **Goal stamps** — blue/red top-down Push Back goals at true field scale (~10").
- Per-element **opacity**, small-radius **snapping**, undo/redo, duplicate,
  z-order, arrow-key nudge, pan/zoom, 24" tile grid + optional field backdrop.
- **Designs** — keep multiple named designs (dropdown to switch, new / rename /
  duplicate / delete). Everything autosaves; **Save** downloads the current design
  as a `.diagram.json` and **Open** loads one back — the portable way to keep a
  library of notebook figures (and the reliable save path in the `file://` build,
  where browser storage can be flaky).
- **Export** — transparent PNG, auto-cropped to content, at Standard / High / Max
  resolution (12 / 20 / 32 px per inch — the SVG is rendered at full pixel size so
  it stays crisp), or copy to clipboard. (Grid/field/handles are never exported.)

## Shortcuts

`V` select · `R` robot · `L` line · `A` arc · `S` spline · `B` boomerang ·
`C` circle · `T` text · `⌫` delete · `⌘Z` / `⌘⇧Z` undo/redo · `⌘D` duplicate ·
arrows nudge (⇧ = 5") · trackpad: scroll to pan, pinch to zoom.

## Notes / assets

- Images live in `src/lib/assets/` (`goal-blue.png`, `goal-red.png`,
  `field-empty.png`) and are **imported** so they inline into the single-file
  build. To use different goal art, replace those PNGs. The goals are clean
  top-down icons sized to the measured ~10" field footprint.
- Chassis + coordinate math is ported from the debugger (`src/lib/geom/`).
- Designs autosave to `localStorage` (`lucaslib-diagram.v2` + one key per design).
  Use **Save** to keep durable/portable copies as `.diagram.json` files.
