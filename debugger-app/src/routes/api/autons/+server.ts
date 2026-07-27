// Dev-server file API for sandbox autons. Reads/writes plain-text route files in
// the "sandbox autons" folder next to the app (process.cwd() = the app dir when
// `vite dev` runs). Backs the saved-autons menu with real files on disk.
//
// NOTE: this is a server endpoint — it runs in the SvelteKit dev server (and any
// Node adapter), NOT in a static export. Fine for the Tauri dev workflow.
import { json, error } from '@sveltejs/kit';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import type { RequestHandler } from './$types';

export const prerender = false;

const DIR = path.join(process.cwd(), 'sandbox autons');

/** filesystem-safe file base for a user name (keeps spaces + basic punctuation) */
const safeBase = (name: string) => (name.replace(/[/\\:*?"<>|]+/g, '_').replace(/\.txt$/i, '').trim() || 'untitled');

async function ensureDir() {
  await fs.mkdir(DIR, { recursive: true });
}

export const GET: RequestHandler = async () => {
  await ensureDir();
  const files = (await fs.readdir(DIR)).filter((f) => f.toLowerCase().endsWith('.txt'));
  const out = [];
  for (const f of files) {
    const full = path.join(DIR, f);
    try {
      const [st, content] = await Promise.all([fs.stat(full), fs.readFile(full, 'utf8')]);
      out.push({ name: f.replace(/\.txt$/i, ''), savedAt: st.mtimeMs, content });
    } catch {
      // skip unreadable entries
    }
  }
  out.sort((a, b) => b.savedAt - a.savedAt);
  return json(out);
};

export const POST: RequestHandler = async ({ request }) => {
  const body = (await request.json()) as { name?: unknown; content?: unknown };
  if (typeof body.name !== 'string' || typeof body.content !== 'string') throw error(400, 'name and content required');
  await ensureDir();
  await fs.writeFile(path.join(DIR, `${safeBase(body.name)}.txt`), body.content, 'utf8');
  return json({ ok: true });
};

export const DELETE: RequestHandler = async ({ url }) => {
  const name = url.searchParams.get('name');
  if (!name) throw error(400, 'name required');
  await ensureDir();
  try {
    await fs.unlink(path.join(DIR, `${safeBase(name)}.txt`));
  } catch {
    // already gone — treat as success
  }
  return json({ ok: true });
};
