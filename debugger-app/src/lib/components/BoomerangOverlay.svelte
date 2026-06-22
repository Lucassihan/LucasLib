<script lang="ts">
  // Live boomerang geometry on the field, shown for the boomerang under the
  // scrubber. Draws the lead axis (end -> initial carrot), the retracting carrot
  // and trailing ghost, the three staging segments (robot -> ghost/carrot/end,
  // with the active stage highlighted), the perpendicular exit plane, and the
  // instantaneous turn arc. Every point uses the trace's own carrot/ghost/arc
  // fields, so it matches the real controller exactly.
  import { toScreen, defaultField, type FieldConfig } from '../field/coords';
  import type { Movement } from '$lib/trace/types';
  import { traceState, tickPairAt, movementAt, poseAt, movementTicks } from '$lib/stores/trace.svelte';

  let { movement, field = defaultField }: { movement: Movement; field?: FieldConfig } = $props();

  const D2R = Math.PI / 180;
  const wrap = (a: number) => {
    while (a > 180) a -= 360;
    while (a < -180) a += 360;
    return a;
  };

  const YELLOW = '#eab308';
  const CYAN = '#22d3ee';
  const PURPLE = '#a78bfa';

  const pair = $derived(tickPairAt(traceState.time));
  const pose = $derived(poseAt(traceState.time));
  const ticks = $derived(movementTicks(movement.index));

  const geo = $derived.by(() => {
    const live = pair.cur;
    const isActive = movementAt(traceState.time)?.index === movement.index;
    if (!isActive || !live || live.mi !== movement.index || ticks.length === 0) return null;

    // s = 1 at the first tick, so its carrot IS the initial (un-retracted) carrot
    const initC = { x: ticks[0].carrotX, y: ticks[0].carrotY };

    const sEnd = toScreen(movement.targetX, movement.targetY, field);
    const sInit = toScreen(initC.x, initC.y, field);
    const sCarrot = toScreen(live.carrotX, live.carrotY, field);
    const sGhost = toScreen(live.ghostX, live.ghostY, field);
    const sRobot = toScreen(pose.x, pose.y, field);

    // perpendicular exit plane through the target (perp to the approach heading)
    const arm = movement.exitPerpLine > 0 ? movement.exitPerpLine : 10;
    const pX = Math.cos(movement.targetH * D2R);
    const pY = -Math.sin(movement.targetH * D2R);
    const pe1 = toScreen(movement.targetX + pX * arm, movement.targetY + pY * arm, field);
    const pe2 = toScreen(movement.targetX - pX * arm, movement.targetY - pY * arm, field);

    // instantaneous turn arc (skip when nearly straight)
    let arc: { cx: number; cy: number; r: number } | null = null;
    const r = live.arcRadius;
    if (r > 0 && r < 160) {
      let dth = pair.prev ? wrap(live.h - pair.prev.h) : 0;
      const fh = live.h * D2R;
      // right-hand normal of the forward vector (sin h, cos h)
      const rnx = Math.cos(fh);
      const rny = -Math.sin(fh);
      const sgn = dth >= 0 ? 1 : -1;
      const c = toScreen(pose.x + rnx * r * sgn, pose.y + rny * r * sgn, field);
      arc = { cx: c.sx, cy: c.sy, r };
    }

    return { sEnd, sInit, sCarrot, sGhost, sRobot, stage: live.stage, pe1, pe2, arc };
  });
</script>

{#if geo}
  {#if geo.arc}
    <circle cx={geo.arc.cx} cy={geo.arc.cy} r={geo.arc.r} fill="none" stroke="#3b82f6" stroke-width="1" stroke-dasharray="2 3" opacity="0.3" vector-effect="non-scaling-stroke" />
  {/if}

  <!-- perpendicular exit plane through the target -->
  <line x1={geo.pe1.sx} y1={geo.pe1.sy} x2={geo.pe2.sx} y2={geo.pe2.sy} stroke="#cfcfcf" stroke-width="1" stroke-dasharray="3 3" opacity="0.3" vector-effect="non-scaling-stroke" />

  <!-- lead axis: end -> initial carrot -->
  <line x1={geo.sEnd.sx} y1={geo.sEnd.sy} x2={geo.sInit.sx} y2={geo.sInit.sy} stroke={YELLOW} stroke-width="1.2" stroke-dasharray="4 3" opacity="0.5" vector-effect="non-scaling-stroke" />

  <!-- staging segments: robot -> ghost / carrot / end (active stage highlighted) -->
  <line x1={geo.sRobot.sx} y1={geo.sRobot.sy} x2={geo.sEnd.sx} y2={geo.sEnd.sy} stroke="#cfcfcf" stroke-width={geo.stage === 2 ? 1.6 : 1} opacity={geo.stage === 2 ? 0.85 : 0.22} vector-effect="non-scaling-stroke" />
  <line x1={geo.sRobot.sx} y1={geo.sRobot.sy} x2={geo.sGhost.sx} y2={geo.sGhost.sy} stroke={PURPLE} stroke-width={geo.stage === 0 ? 1.6 : 1} opacity={geo.stage === 0 ? 0.85 : 0.3} vector-effect="non-scaling-stroke" />
  <line x1={geo.sRobot.sx} y1={geo.sRobot.sy} x2={geo.sCarrot.sx} y2={geo.sCarrot.sy} stroke={CYAN} stroke-width={geo.stage === 1 ? 1.6 : 1} opacity={geo.stage === 1 ? 0.9 : 0.3} vector-effect="non-scaling-stroke" />

  <!-- initial carrot (anchor) -->
  <circle cx={geo.sInit.sx} cy={geo.sInit.sy} r="2.2" fill="none" stroke="#6a6a6a" stroke-width="1.4" vector-effect="non-scaling-stroke" />

  <!-- ghost -->
  {#if geo.stage === 0}
    <circle cx={geo.sGhost.sx} cy={geo.sGhost.sy} r="3.6" fill="none" stroke={PURPLE} stroke-width="1" opacity="0.5" vector-effect="non-scaling-stroke" />
  {/if}
  <circle cx={geo.sGhost.sx} cy={geo.sGhost.sy} r="2.2" fill="none" stroke={PURPLE} stroke-width="1.4" vector-effect="non-scaling-stroke" />

  <!-- live carrot (filled; ringed when it is the active steer target) -->
  {#if geo.stage === 1}
    <circle cx={geo.sCarrot.sx} cy={geo.sCarrot.sy} r="4.4" fill="none" stroke={CYAN} stroke-width="1" opacity="0.5" vector-effect="non-scaling-stroke" />
  {/if}
  <circle cx={geo.sCarrot.sx} cy={geo.sCarrot.sy} r="2.8" fill={CYAN} />
{/if}
