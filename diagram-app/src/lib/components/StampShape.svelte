<script lang="ts">
  import type { StampEl } from '$lib/model/types';
  import goalBlue from '$lib/assets/goal-blue.png';
  import goalRed from '$lib/assets/goal-red.png';

  let { el }: { el: StampEl } = $props();
  const srcMap: Record<string, string> = { 'goal-blue': goalBlue, 'goal-red': goalRed };
  const src = $derived(srcMap[el.kind]);
</script>

<g data-el={el.id} opacity={el.opacity} style="cursor: move">
  <g transform="translate({el.x},{-el.y}) rotate({el.rotation})">
    <image href={src} x={-el.w / 2} y={-el.h / 2} width={el.w} height={el.h} preserveAspectRatio="xMidYMid meet" style="pointer-events: none" />
    <rect x={-el.w / 2} y={-el.h / 2} width={el.w} height={el.h} fill="transparent" style="pointer-events: all" />
  </g>
</g>
