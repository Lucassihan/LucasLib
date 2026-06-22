// One-shot: split lyap-mockups.html into per-card pages and render each to a
// tightly-cropped PNG via headless Chrome. Output goes to /tmp/lyap-mockups/.

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const src = readFileSync(join(here, '..', 'static', 'lyap-mockups.html'), 'utf8');

const headMatch = src.match(/<head>([\s\S]*?)<\/head>/);
const head = headMatch ? headMatch[1] : '';

const ids = ['L1', 'L2', 'L3'];
const sizes = { L1: { w: 560, h: 360 }, L2: { w: 560, h: 320 }, L3: { w: 560, h: 310 } };

const staticDir = join(here, '..', 'static');
const outDir = '/tmp/lyap-mockups';
mkdirSync(outDir, { recursive: true });

const cardRe = (id) =>
  new RegExp(`<div class="card" id="${id}">[\\s\\S]*?<\\/div>\\s*<\\/div>\\s*<\\/div>`);

for (const id of ids) {
  const m = src.match(cardRe(id));
  if (!m) {
    console.error('missing', id);
    continue;
  }
  const body = `<div class="page" style="padding:8px;gap:0"><div class="row" style="grid-template-columns:1fr;gap:0">${m[0]}</div></div>`;
  const page = `<!doctype html><html><head>${head}<style>html,body{background:transparent;margin:0;padding:0}.card{margin:0}</style></head><body>${body}</body></html>`;
  const htmlPath = join(staticDir, `__lyap_${id}.html`);
  writeFileSync(htmlPath, page);

  const { w, h } = sizes[id];
  const out = join(outDir, `${id}.png`);
  execFileSync(
    '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    [
      '--headless',
      '--no-sandbox',
      '--disable-gpu',
      '--hide-scrollbars',
      '--default-background-color=00000000',
      `--window-size=${w},${h}`,
      `--screenshot=${out}`,
      `http://localhost:1420/__lyap_${id}.html`
    ],
    { stdio: 'ignore' }
  );
  console.log('wrote', out);
}
