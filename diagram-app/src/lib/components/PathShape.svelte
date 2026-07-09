<script lang="ts">
  import RobotShape from './RobotShape.svelte';
  import type { PathEl } from '$lib/model/types';
  import { samplePath, poseAtT } from '$lib/geom/paths';

  let { el, scale }: { el: PathEl; scale: number } = $props();

  const sp = $derived(samplePath(el));
  // svg-world points (negate y)
  const svgPts = $derived(sp.pts.map((p) => ({ x: p.x, y: -p.y })));
  const pointsStr = $derived(svgPts.map((p) => `${p.x},${p.y}`).join(' '));

  const dashArr = $derived.by(() => {
    const w = el.width;
    if (el.dash === 'dashed') return `${w * 3.2} ${w * 2.4}`;
    if (el.dash === 'dotted') return `0 ${w * 2}`;
    return undefined;
  });

  const hitW = $derived(Math.max(el.width * 2.5, 9 / scale));

  function arrow(tip: { x: number; y: number }, from: { x: number; y: number }) {
    const dx = tip.x - from.x;
    const dy = tip.y - from.y;
    const L = Math.hypot(dx, dy) || 1;
    const ux = dx / L;
    const uy = dy / L;
    const size = el.width * 3.6;
    const halfW = el.width * 2.4;
    const bx = tip.x - ux * size;
    const by = tip.y - uy * size;
    const px = -uy;
    const py = ux;
    return `${tip.x},${tip.y} ${bx + px * halfW},${by + py * halfW} ${bx - px * halfW},${by - py * halfW}`;
  }

  const endArrow = $derived(
    (el.arrows === 'end' || el.arrows === 'both') && svgPts.length >= 2
      ? arrow(svgPts[svgPts.length - 1], svgPts[svgPts.length - 2])
      : null
  );
  const startArrow = $derived(
    (el.arrows === 'start' || el.arrows === 'both') && svgPts.length >= 2
      ? arrow(svgPts[0], svgPts[1])
      : null
  );

  // boomerang extras
  const boom = $derived(el.kind === 'boomerang' ? sp.boom : undefined);
  const scrub = $derived(
    el.kind === 'boomerang' && el.boom && el.boom.robotT != null ? poseAtT(sp, el.boom.robotT) : null
  );
  const liveCarrot = $derived.by(() => {
    if (!boom || !el.boom || el.boom.robotT == null) return null;
    // find the carrot/ghost at the scrub fraction
    const t = el.boom.robotT;
    const s = boom.samples;
    const idx = Math.min(s.length - 1, Math.max(0, Math.round(t * (s.length - 1))));
    return s[idx];
  });
</script>

<g data-el={el.id} opacity={el.opacity}>
  <!-- fat transparent hit area -->
  <polyline points={pointsStr} fill="none" stroke="transparent" stroke-width={hitW} style="cursor: move" />

  <polyline
    points={pointsStr}
    fill="none"
    stroke={el.color}
    stroke-width={el.width}
    stroke-dasharray={dashArr}
    stroke-linejoin="round"
    stroke-linecap="round"
    style="pointer-events: none"
  />

  {#if startArrow}
    <polygon points={startArrow} fill={el.color} style="pointer-events: none" />
  {/if}
  {#if endArrow}
    <polygon points={endArrow} fill={el.color} style="pointer-events: none" />
  {/if}

  {#if boom && el.boom?.showLeads}
    <!-- lead axis: target → initial carrot -->
    <line
      x1={boom.target.x}
      y1={-boom.target.y}
      x2={boom.initCarrot.x}
      y2={-boom.initCarrot.y}
      stroke="#eab308"
      stroke-width={el.width * 0.8}
      stroke-dasharray={`${el.width * 2.5} ${el.width * 2}`}
      opacity="0.75"
      style="pointer-events: none"
    />
    <circle cx={boom.initCarrot.x} cy={-boom.initCarrot.y} r={el.width * 1.6} fill="none" stroke="#a16207" stroke-width={el.width * 0.7} style="pointer-events: none" />

    {#if liveCarrot && scrub}
      <line x1={scrub.x} y1={-scrub.y} x2={liveCarrot.carrot.x} y2={-liveCarrot.carrot.y} stroke="#22d3ee" stroke-width={el.width * 0.7} opacity="0.7" style="pointer-events: none" />
      <circle cx={liveCarrot.ghost.x} cy={-liveCarrot.ghost.y} r={el.width * 1.5} fill="none" stroke="#a78bfa" stroke-width={el.width * 0.8} style="pointer-events: none" />
      <circle cx={liveCarrot.carrot.x} cy={-liveCarrot.carrot.y} r={el.width * 1.6} fill="#22d3ee" style="pointer-events: none" />
    {/if}
  {/if}

  {#if scrub && el.boom}
    <g transform="translate({scrub.x},{-scrub.y}) rotate({scrub.heading})" style="pointer-events: none">
      <RobotShape chassis={el.boom.chassis} color={el.color} fill={true} />
    </g>
  {/if}
</g>
