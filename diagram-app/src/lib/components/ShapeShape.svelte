<script lang="ts">
  import type { ShapeEl } from '$lib/model/types';

  let { el }: { el: ShapeEl } = $props();

  const dashArr = $derived.by(() => {
    const w = el.strokeWidth;
    if (el.dash === 'dashed') return `${w * 3.2} ${w * 2.4}`;
    if (el.dash === 'dotted') return `0 ${w * 2}`;
    return undefined;
  });
</script>

<g data-el={el.id} opacity={el.opacity} style="cursor: move">
  {#if el.shape === 'circle'}
    <circle cx={el.x} cy={-el.y} r={el.w / 2} fill="transparent" style="pointer-events: all" />
    <circle
      cx={el.x}
      cy={-el.y}
      r={el.w / 2}
      fill={el.fill}
      stroke={el.stroke}
      stroke-width={el.strokeWidth}
      stroke-dasharray={dashArr}
      stroke-linecap="round"
      style="pointer-events: none"
    />
  {:else}
    <rect x={el.x - el.w / 2} y={-el.y - el.h / 2} width={el.w} height={el.h} fill="transparent" style="pointer-events: all" />
    <rect
      x={el.x - el.w / 2}
      y={-el.y - el.h / 2}
      width={el.w}
      height={el.h}
      rx="0.6"
      fill={el.fill}
      stroke={el.stroke}
      stroke-width={el.strokeWidth}
      stroke-dasharray={dashArr}
      stroke-linejoin="round"
      style="pointer-events: none"
    />
  {/if}
</g>
