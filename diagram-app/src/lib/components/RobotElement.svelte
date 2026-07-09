<script lang="ts">
  import RobotShape from './RobotShape.svelte';
  import type { RobotEl } from '$lib/model/types';
  import { holesToInches } from '$lib/geom/chassis';

  let { el }: { el: RobotEl } = $props();
  const labelY = $derived(-el.y - (holesToInches(el.chassis.lengthHoles) / 2 + 3));
</script>

<g data-el={el.id} opacity={el.opacity} style="cursor: move">
  <g transform="translate({el.x},{-el.y}) rotate({el.heading})">
    <RobotShape chassis={el.chassis} color={el.color} fill={el.fill} />
  </g>
  {#if el.label}
    <text
      x={el.x}
      y={labelY}
      font-size="5"
      font-weight="600"
      text-anchor="middle"
      fill={el.color}
      style="pointer-events: none">{el.label}</text>
  {/if}
</g>
