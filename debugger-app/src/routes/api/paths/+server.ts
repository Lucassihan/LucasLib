// Dev-server file API for Trajectory-Generator paths. Reads/writes JSON path
// files in the "trajectory paths" folder next to the app (process.cwd() = the app
// dir when `vite dev` runs). Backs the saved-paths drawer with real files on disk.
// Sibling of the sandbox autons endpoint — same shape, different folder + .json.
import { json, error } from '@sveltejs/kit';
import { promises as fs } from 'node:fs';
import path from 'node:path';
import type { RequestHandler } from './$types';

export const prerender = false;

const DIR = path.join(process.cwd(), 'trajectory paths');

const safeBase = (name: string) => name.replace(/[/\\:*?"<>|]+/g, '_').replace(/\.json$/i, '').trim() || 'untitled';

async function ensureDir() {
  await fs.mkdir(DIR, { recursive: true });
}

export const GET: RequestHandler = async () => {
  await ensureDir();
  const files = (await fs.readdir(DIR)).filter((f) => f.toLowerCase().endsWith('.json'));
  const out = [];
  for (const f of files) {
    const full = path.join(DIR, f);
    try {
      const [st, content] = await Promise.all([fs.stat(full), fs.readFile(full, 'utf8')]);
      out.push({ name: f.replace(/\.json$/i, ''), savedAt: st.mtimeMs, content });
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
  await fs.writeFile(path.join(DIR, `${safeBase(body.name)}.json`), body.content, 'utf8');
  return json({ ok: true });
};

export const DELETE: RequestHandler = async ({ url }) => {
  const name = url.searchParams.get('name');
  if (!name) throw error(400, 'name required');
  await ensureDir();
  try {
    await fs.unlink(path.join(DIR, `${safeBase(name)}.json`));
  } catch {
    // already gone — treat as success
  }
  return json({ ok: true });
};
