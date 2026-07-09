<script lang="ts">
  import type { TextEl } from '$lib/model/types';

  let { el }: { el: TextEl } = $props();

  const lines = $derived(el.text.split('\n'));
  const anchor = $derived(el.align === 'center' ? 'middle' : el.align === 'right' ? 'end' : 'start');
  // rough hit box, in the text's LOCAL frame (anchor at origin, baseline at y=0)
  const maxLen = $derived(Math.max(1, ...lines.map((l) => l.length)));
  const boxW = $derived(maxLen * el.size * 0.58);
  const boxH = $derived(lines.length * el.size * 1.25);
  const boxX = $derived(el.align === 'center' ? -boxW / 2 : el.align === 'right' ? -boxW : 0);
</script>

<g data-el={el.id} opacity={el.opacity} style="cursor: move">
  <g transform="translate({el.x},{-el.y}) rotate({el.rotation ?? 0})">
    <rect x={boxX} y={-el.size} width={boxW} height={boxH} fill="transparent" style="pointer-events: all" />
    <text
      x="0"
      y="0"
      font-size={el.size}
      fill={el.color}
      text-anchor={anchor}
      font-weight={el.bold ? 700 : 400}
      font-style={el.italic ? 'italic' : 'normal'}
      style="pointer-events: none; user-select: none"
    >
      {#each lines as ln, i}
        <tspan x="0" dy={i === 0 ? 0 : el.size * 1.25}>{ln}</tspan>
      {/each}
    </text>
  </g>
</g>
