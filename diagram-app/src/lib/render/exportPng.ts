// Rasterize the diagram SVG to a transparent PNG, auto-cropped to content with a
// small padding, at a chosen DPI scale. Grid, field backdrop, selection UI and
// handles are stripped; stamp/field images are inlined so the canvas isn't
// tainted and the sub-images actually render.

export interface ExportOpts {
  scale?: number; // pixels per world-inch (higher = crisper / bigger)
  pad?: number; // world-unit padding around content
}

async function inlineImages(root: SVGSVGElement): Promise<void> {
  const imgs = Array.from(root.querySelectorAll('image'));
  await Promise.all(
    imgs.map(async (im) => {
      const href = im.getAttribute('href') || im.getAttribute('xlink:href');
      if (!href || href.startsWith('data:')) return;
      try {
        const res = await fetch(href);
        const blob = await res.blob();
        const dataUrl = await new Promise<string>((resolve) => {
          const fr = new FileReader();
          fr.onload = () => resolve(fr.result as string);
          fr.readAsDataURL(blob);
        });
        im.setAttribute('href', dataUrl);
      } catch {
        /* leave as-is */
      }
    })
  );
}

export async function renderPNG(opts: ExportOpts = {}): Promise<Blob | null> {
  const scale = opts.scale ?? 20;
  const pad = opts.pad ?? 3;

  const live = document.getElementById('diagram-canvas') as SVGSVGElement | null;
  if (!live) return null;

  const clone = live.cloneNode(true) as SVGSVGElement;
  const inner = clone.querySelector('g') as SVGGElement | null;
  if (!inner) return null;

  // strip non-content layers (grid, field backdrop, selection UI, handles)
  clone.querySelectorAll('.grid, .field-bg, .selbox, .handle, .snap').forEach((n) => n.remove());
  inner.removeAttribute('transform');

  await inlineImages(clone);

  // measure content bbox offscreen
  clone.setAttribute('style', 'position:absolute;left:-99999px;top:0;');
  clone.setAttribute('width', '1000');
  clone.setAttribute('height', '1000');
  document.body.appendChild(clone);
  const bb = inner.getBBox();
  document.body.removeChild(clone);

  if (bb.width === 0 || bb.height === 0) return null;

  const vbX = bb.x - pad;
  const vbY = bb.y - pad;
  const vbW = bb.width + pad * 2;
  const vbH = bb.height + pad * 2;

  // Give the SVG its FINAL pixel dimensions (viewBox stays in world units) so the
  // browser renders it as crisp vector at full resolution — not a tiny thumbnail
  // scaled up. `scale` is pixels-per-inch.
  const pxW = Math.max(1, Math.round(vbW * scale));
  const pxH = Math.max(1, Math.round(vbH * scale));

  clone.removeAttribute('style');
  clone.setAttribute('viewBox', `${vbX} ${vbY} ${vbW} ${vbH}`);
  clone.setAttribute('width', String(pxW));
  clone.setAttribute('height', String(pxH));
  clone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');

  const svgText = new XMLSerializer().serializeToString(clone);
  const svgUrl = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svgText);

  const img = new Image();
  img.width = pxW;
  img.height = pxH;
  const loaded = new Promise<void>((resolve, reject) => {
    img.onload = () => resolve();
    img.onerror = reject;
  });
  img.src = svgUrl;
  await loaded;

  const canvas = document.createElement('canvas');
  canvas.width = pxW;
  canvas.height = pxH;
  const ctx = canvas.getContext('2d')!;
  ctx.drawImage(img, 0, 0, pxW, pxH);

  return await new Promise<Blob | null>((resolve) => canvas.toBlob(resolve, 'image/png'));
}

export async function downloadPNG(name = 'diagram', opts: ExportOpts = {}): Promise<boolean> {
  const blob = await renderPNG(opts);
  if (!blob) return false;
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `${name}.png`;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
  return true;
}

export async function copyPNG(opts: ExportOpts = {}): Promise<boolean> {
  const blob = await renderPNG(opts);
  if (!blob) return false;
  try {
    await navigator.clipboard.write([new ClipboardItem({ 'image/png': blob })]);
    return true;
  } catch {
    return false;
  }
}
