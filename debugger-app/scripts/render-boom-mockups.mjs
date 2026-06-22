// Brainstorm mockups for the new (ghost-point) boomerang visualizer.
// Generates one standalone HTML per card under static/, then screenshots each
// to /tmp/boom-mockups/<id>.png via headless Chrome (preview server must serve
// http://localhost:1420). Run: node scripts/render-boom-mockups.mjs

import { writeFileSync, mkdirSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const staticDir = join(here, '..', 'static');
const outDir = '/tmp/boom-mockups';
mkdirSync(outDir, { recursive: true });

const CSS = `
  *{box-sizing:border-box}
  html,body{margin:0;padding:0;background:#0e0e0e;font-family:'Manrope',system-ui,sans-serif;color:#ededed}
  .wrap{padding:10px}
  .card{background:#181818;border-radius:11px;padding:15px}
  .cap{font-size:14px;font-weight:600;margin:0 0 3px}
  .cd{font-size:12px;color:#9a9a9a;margin:0 0 11px;line-height:1.45}
  .app{background:#1b1b1b;border-radius:9px;padding:13px;box-shadow:inset 0 0 0 1px rgba(255,255,255,0.05)}
  .hd{display:flex;align-items:center;gap:7px;margin-bottom:11px}
  .hl{font-size:11px;font-weight:700}
  .hb{font-size:8.5px;font-weight:700;color:#3a2c06;background:#eab308;border-radius:3px;padding:1px 6px;letter-spacing:.04em}
  .hs{font-size:9px;color:#6a6a6a;margin-left:auto}
  .leg{display:flex;flex-wrap:wrap;gap:9px 14px;margin-top:11px;font-size:10px;color:#9a9a9a}
  .leg span{display:inline-flex;align-items:center;gap:5px}
  .dot{width:8px;height:8px;border-radius:50%;display:inline-block}
  .ring{width:8px;height:8px;border-radius:50%;border:1.5px solid;display:inline-block;box-sizing:border-box}
  .dash{width:13px;height:0;border-top:2px dashed;display:inline-block}
  .ln{width:13px;height:0;border-top:2px solid;display:inline-block}
  .badges{display:flex;gap:6px;margin-bottom:13px}
  .badge{flex:1;text-align:center;border-radius:6px;padding:7px 4px;background:#141414;border:1px solid #262626}
  .badge .bt{font-size:11px;font-weight:700;letter-spacing:.05em}
  .badge .bsub{font-size:8px;color:#6a6a6a;margin-top:2px}
  .badge.on{background:#0c2630;border-color:#22d3ee}
  .badge.on .bt{color:#22d3ee}
  .gauge{height:8px;border-radius:4px;background:#141414;overflow:hidden;margin:3px 0}
  .gfill{height:100%;border-radius:4px}
  .kv{display:flex;justify-content:space-between;align-items:baseline;margin:5px 0;font-size:11px}
  .k{color:#8a8a8a}
  .v{font-family:ui-monospace,Menlo,monospace;font-weight:600}
  .cn{font-size:10px;font-weight:600;color:#b9b9b9;margin:11px 0 6px;letter-spacing:.02em}
  .term{display:flex;align-items:center;gap:8px;margin-bottom:6px}
  .tl{width:64px;font-size:10px;color:#9a9a9a}
  .bar{flex:1;height:7px;border-radius:3.5px;background:#141414;overflow:hidden}
  .fill{height:100%;border-radius:3.5px}
  .tv{width:58px;text-align:right;font-family:ui-monospace,Menlo,monospace;font-size:10px;font-weight:600}
  .pill{font-size:9px;font-weight:700;border-radius:3px;padding:1px 6px}
  .note{font-size:9px;color:#6a6a6a;margin-top:9px;line-height:1.4}
  .sub{font-size:8px;color:#7a7a7a;display:block}
  .big{font-size:12px;font-weight:600}
`;

// ── Field-overlay "money view" SVG ───────────────────────────────────────────
const GEO_SVG = `
<svg viewBox="0 0 330 268" width="100%" xmlns="http://www.w3.org/2000/svg">
  <!-- instantaneous turn arc (travelArcRadius), tangent at the robot -->
  <circle cx="129" cy="286" r="116" fill="none" stroke="#3b82f6" stroke-width="1" stroke-dasharray="3 4" opacity="0.35"/>
  <text x="150" y="196" font-size="8" fill="#5d82bd" font-family="sans-serif">arc r</text>

  <!-- perpLine activation circle + exit plane through target -->
  <circle cx="228" cy="52" r="23" fill="none" stroke="#cfcfcf" stroke-width="1" stroke-dasharray="2 3" opacity="0.25"/>
  <line x1="190" y1="52" x2="266" y2="52" stroke="#cfcfcf" stroke-width="1" stroke-dasharray="4 3" opacity="0.3"/>
  <text x="268" y="50" font-size="7.5" fill="#8a8a8a" font-family="sans-serif">exit plane</text>

  <!-- lead axis: end -> carrot -> ghost -> initial carrot -->
  <line x1="228" y1="52" x2="228" y2="212" stroke="#eab308" stroke-width="1.3" stroke-dasharray="5 3" opacity="0.5"/>

  <!-- retraction direction along the axis -->
  <line x1="238" y1="202" x2="238" y2="128" stroke="#22d3ee" stroke-width="1" opacity="0.55"/>
  <polygon points="238,124 235,132 241,132" fill="#22d3ee" opacity="0.7"/>
  <text x="244" y="170" font-size="7.5" fill="#3aa9bd" font-family="sans-serif">retract</text>

  <!-- robot path trail -->
  <path d="M28,232 Q52,206 88,176" fill="none" stroke="#cfcfcf" stroke-width="1.5" opacity="0.4"/>

  <!-- staging segments robot -> ghost / carrot / end -->
  <line x1="88" y1="176" x2="228" y2="52"  stroke="#cfcfcf" stroke-width="1" opacity="0.28"/>
  <line x1="88" y1="176" x2="228" y2="152" stroke="#a78bfa" stroke-width="1" opacity="0.4"/>
  <line x1="88" y1="176" x2="228" y2="120" stroke="#22d3ee" stroke-width="1.5" opacity="0.85"/>

  <!-- target end pose + final heading arrow -->
  <line x1="228" y1="52" x2="228" y2="30" stroke="#eab308" stroke-width="1.6"/>
  <polygon points="228,26 224,34 232,34" fill="#eab308"/>
  <circle cx="228" cy="52" r="4" fill="#eab308"/>
  <text x="236" y="46" font-size="8" fill="#d9c168" font-family="sans-serif">end</text>

  <!-- initial carrot (anchor) -->
  <circle cx="228" cy="206" r="4" fill="none" stroke="#6a6a6a" stroke-width="1.5"/>
  <text x="236" y="209" font-size="8" fill="#7a7a7a" font-family="sans-serif">carrot0</text>

  <!-- ghost -->
  <circle cx="228" cy="152" r="4" fill="none" stroke="#a78bfa" stroke-width="1.5"/>
  <text x="204" y="166" font-size="8" fill="#a78bfa" font-family="sans-serif">ghost</text>

  <!-- live carrot (active steer target this stage) -->
  <circle cx="228" cy="120" r="8" fill="none" stroke="#22d3ee" stroke-width="1" opacity="0.4"/>
  <circle cx="228" cy="120" r="5" fill="#22d3ee"/>
  <text x="238" y="116" font-size="8" fill="#22d3ee" font-family="sans-serif">carrot (steer)</text>

  <!-- robot chassis, facing the carrot; intake on the leading edge -->
  <g transform="translate(88,176) rotate(64)">
    <rect x="-12" y="-14" width="24" height="28" rx="3" fill="rgba(204,204,204,0.13)" stroke="#cfcfcf" stroke-width="1.3"/>
    <rect x="-8" y="-16.5" width="4" height="5" rx="1" fill="#cfcfcf"/>
    <rect x="-2" y="-16.5" width="4" height="5" rx="1" fill="#cfcfcf"/>
    <rect x="4" y="-16.5" width="4" height="5" rx="1" fill="#cfcfcf"/>
    <rect x="-11" y="-10" width="2.6" height="8" rx="1" fill="none" stroke="#cfcfcf" stroke-width="1" opacity="0.6"/>
    <rect x="8.4" y="-10" width="2.6" height="8" rx="1" fill="none" stroke="#cfcfcf" stroke-width="1" opacity="0.6"/>
    <circle cx="0" cy="0" r="1.4" fill="#e8e8e8"/>
  </g>
  <text x="44" y="172" font-size="8" fill="#bdbdbd" font-family="sans-serif">robot</text>
</svg>`;

const GEO_LEGEND = `
<div class="leg">
  <span><i class="dot" style="background:#eab308"></i>end (target pose)</span>
  <span><i class="dot" style="background:#22d3ee"></i>carrot — active steer</span>
  <span><i class="ring" style="border-color:#a78bfa"></i>ghost</span>
  <span><i class="ring" style="border-color:#6a6a6a"></i>carrot0 (anchor)</span>
  <span><i class="dash" style="border-color:#cfcfcf"></i>perp / exit plane</span>
  <span><i class="dash" style="border-color:#3b82f6"></i>turn arc r</span>
</div>`;

const cards = [
  {
    id: 'GEO',
    w: 620,
    h: 520,
    html: `<div class="card">
      <div class="cap">A · Field overlay — the money view</div>
      <div class="cd">One snapshot of the whole geometry: the three collinear lead points sliding along the target axis, the live staging segments, the retraction sweep, and the slip / exit guides. The active steer point is highlighted per stage.</div>
      <div class="app">
        <div class="hd"><span class="hl">Boomerang geometry</span><span class="hb">MID</span><span class="hs">@ cursor</span></div>
        ${GEO_SVG}
        ${GEO_LEGEND}
      </div>
    </div>`
  },
  {
    id: 'STAGE',
    w: 560,
    h: 360,
    html: `<div class="card">
      <div class="cap">B · Stage & retraction HUD</div>
      <div class="cd">The "where am I in the move" panel. Latched stage, the single most useful number (retraction s), and the staging distances against the ghost→carrot switch threshold.</div>
      <div class="app">
        <div class="hd"><span class="hl">Stage</span><span class="hs">latches one-way</span></div>
        <div class="badges">
          <div class="badge"><div class="bt">FAR</div><div class="bsub">steer ghost</div></div>
          <div class="badge on"><div class="bt">MID</div><div class="bsub">steer carrot</div></div>
          <div class="badge"><div class="bt">END</div><div class="bsub">drive to end</div></div>
        </div>
        <div class="kv"><span class="k">retraction&nbsp; s</span><span class="v">0.41</span></div>
        <div class="gauge"><div class="gfill" style="width:41%;background:#22d3ee"></div></div>
        <div class="kv"><span class="k">lead displacement&nbsp; s·initialLeadDist</span><span class="v">9.8 in</span></div>
        <div style="height:9px"></div>
        <div class="kv"><span class="k">dist → end</span><span class="v">14.2 in</span></div>
        <div class="kv"><span class="k">dist → carrot</span><span class="v" style="color:#22d3ee">6.1 in</span></div>
        <div class="kv"><span class="k">dist → ghost <span class="sub" style="display:inline">vs switch 3.3 in</span></span><span class="v">7.9 in</span></div>
        <div class="note">latches: closeGhost ✓ &nbsp;·&nbsp; closeEnd ✗ &nbsp;|&nbsp; initCarrotDist 24.1 · minCarrotDist 9.9</div>
      </div>
    </div>`
  },
  {
    id: 'PIPE',
    w: 560,
    h: 440,
    html: `<div class="card">
      <div class="cap">C · Control pipeline & slip cap</div>
      <div class="cd">The voltage path from raw PID → blended → governed motor volts, plus the two throttles unique to this boomerang: the cosine multiplier and the centripetal slip cap.</div>
      <div class="app">
        <div class="hd"><span class="hl">Control pipeline</span><span class="hs">volts</span></div>
        <div class="cn">Pre-blend (raw PID)</div>
        <div class="term"><span class="tl">drivePow</span><div class="bar"><div class="fill" style="width:52%;background:#3b82f6"></div></div><span class="tv">+6.20</span></div>
        <div class="term"><span class="tl">headingPow</span><div class="bar"><div class="fill" style="width:34%;background:#ef4444"></div></div><span class="tv">-4.05</span></div>
        <div class="cn">Throttles</div>
        <div class="kv"><span class="k">× cos(eθ)</span><span class="v">0.94</span></div>
        <div class="kv"><span class="k">slip cap √(chasePower·|r|) <span class="sub" style="display:inline">r = -22 in</span></span><span class="v" style="color:#f87171">5.1 V&nbsp; <span class="pill" style="background:#3a1414;color:#f87171">CLAMP</span></span></div>
        <div class="cn">Post-blend → motors</div>
        <div class="term"><span class="tl">driveOut</span><div class="bar"><div class="fill" style="width:43%;background:#3b82f6"></div></div><span class="tv">+5.10</span></div>
        <div class="term"><span class="tl">turnOut</span><div class="bar"><div class="fill" style="width:34%;background:#ef4444"></div></div><span class="tv">-4.05</span></div>
        <div class="term"><span class="tl">left</span><div class="bar"><div class="fill" style="width:9%;background:#e8e8e8"></div></div><span class="tv">+1.05</span></div>
        <div class="term"><span class="tl">right</span><div class="bar"><div class="fill" style="width:76%;background:#e8e8e8"></div></div><span class="tv">+9.15</span></div>
        <div class="note">govern cap: OK (max 9.15 &lt; 12) — turn+drive ratio preserved</div>
      </div>
    </div>`
  },
  {
    id: 'EXIT',
    w: 560,
    h: 340,
    html: `<div class="card">
      <div class="cap">D · Exit health</div>
      <div class="cd">Why the move will (or won't) end yet: timeout, both settle windows filling, the perp-line latch, cross-track alignment for the final approach, and live speed.</div>
      <div class="app">
        <div class="hd"><span class="hl">Exit health</span><span class="hs">not exited</span></div>
        <div class="kv"><span class="k">elapsed</span><span class="v">1.84 / 3.00 s</span></div>
        <div class="gauge"><div class="gfill" style="width:61%;background:#6a6a6a"></div></div>
        <div class="kv"><span class="k">small window (0.5 in)</span><span class="v">120 / 200 ms</span></div>
        <div class="gauge"><div class="gfill" style="width:60%;background:#34d399"></div></div>
        <div class="kv"><span class="k">big window (3 in)</span><span class="v">0 / 500 ms</span></div>
        <div class="gauge"><div class="gfill" style="width:0%;background:#34d399"></div></div>
        <div style="height:8px"></div>
        <div class="kv"><span class="k">perpLine</span><span class="v">armed · sign + (not crossed)</span></div>
        <div class="kv"><span class="k">cross-track to θ-line</span><span class="v">1.2 in</span></div>
        <div class="kv"><span class="k">chassis.velocity</span><span class="v">31 in/s</span></div>
      </div>
    </div>`
  },
  {
    id: 'LAYOUT',
    w: 620,
    h: 380,
    html: `<div class="card">
      <div class="cap">E · How it could sit in the app</div>
      <div class="cd">The overlay rides the existing field map; the three HUD panels stack in a "Boomerang" inspector that only appears for boomerang moves. Secondary overlay layers (arc, perp, trails) are toggle-able so the money view doesn't get noisy.</div>
      <div class="app">
        <div style="display:flex;gap:10px;height:250px">
          <div style="flex:1.5;background:#141414;border-radius:7px;position:relative;display:flex;align-items:center;justify-content:center">
            <span style="font-size:10px;color:#6a6a6a;position:absolute;top:8px;left:10px">Field + overlay</span>
            <svg viewBox="0 0 150 150" width="150" height="150">
              <line x1="100" y1="30" x2="100" y2="120" stroke="#eab308" stroke-width="1" stroke-dasharray="4 3" opacity="0.5"/>
              <circle cx="100" cy="30" r="3" fill="#eab308"/>
              <circle cx="100" cy="70" r="3.5" fill="#22d3ee"/>
              <circle cx="100" cy="92" r="3" fill="none" stroke="#a78bfa" stroke-width="1.3"/>
              <line x1="48" y1="104" x2="100" y2="70" stroke="#22d3ee" stroke-width="1.2" opacity="0.8"/>
              <g transform="translate(48,104) rotate(58)"><rect x="-8" y="-9" width="16" height="18" rx="2.5" fill="rgba(204,204,204,0.13)" stroke="#cfcfcf" stroke-width="1.1"/></g>
            </svg>
            <span style="font-size:8px;color:#5a5a5a;position:absolute;bottom:8px;left:10px">▣ arc &nbsp; ▣ perp &nbsp; ▢ trails</span>
          </div>
          <div style="flex:1;display:flex;flex-direction:column;gap:8px">
            <div style="flex:1;background:#141414;border-radius:7px;padding:8px"><span style="font-size:9px;color:#22d3ee">Stage · MID</span><div style="font-size:8px;color:#6a6a6a;margin-top:3px">s 0.41 · disp 9.8in</div></div>
            <div style="flex:1;background:#141414;border-radius:7px;padding:8px"><span style="font-size:9px;color:#9a9a9a">Pipeline</span><div style="font-size:8px;color:#6a6a6a;margin-top:3px">L 1.05 · R 9.15 · CLAMP</div></div>
            <div style="flex:1;background:#141414;border-radius:7px;padding:8px"><span style="font-size:9px;color:#9a9a9a">Exit health</span><div style="font-size:8px;color:#6a6a6a;margin-top:3px">1.84/3.0s · perp armed</div></div>
          </div>
        </div>
      </div>
    </div>`
  }
];

for (const c of cards) {
  const page = `<!doctype html><html><head><meta charset="utf-8"><style>${CSS}</style></head><body><div class="wrap">${c.html}</div></body></html>`;
  const htmlPath = join(staticDir, `__boom_${c.id}.html`);
  writeFileSync(htmlPath, page);
  const out = join(outDir, `${c.id}.png`);
  execFileSync(
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    [
      '--headless',
      '--no-sandbox',
      '--disable-gpu',
      '--hide-scrollbars',
      `--window-size=${c.w},${c.h}`,
      `--screenshot=${out}`,
      `http://localhost:1420/__boom_${c.id}.html`
    ],
    { stdio: 'ignore' }
  );
  console.log('wrote', out);
}
