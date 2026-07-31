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

  /** Sandbox override: full live geometry, bypassing the trace store. */
  export interface BoomOverride {
    end: { x: number; y: number };
    targetH: number;
    /** ec.d — ⊥ trip-line offset back toward the robot; -1 = cross off */
    perpLine: number;
    backward: boolean;
    init: { x: number; y: number };
    carrot: { x: number; y: number };
    ghost: { x: number; y: number };
    robot: { x: number; y: number; h: number };
    prevH: number;
    stage: number;
    arcR: number;
  }

  let {
    movement,
    field = defaultField,
    override = undefined
  }: { movement?: Movement; field?: FieldConfig; override?: BoomOverride } = $props();

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
  const ticks = $derived(movement ? movementTicks(movement.index) : []);

  /** shared geometry builder — identical drawing for trace data and overrides */
  function buildGeo(
    end: { x: number; y: number },
    targetH: number,
    perpLine: number,
    perpBack: boolean,
    init: { x: number; y: number },
    carrot: { x: number; y: number },
    ghost: { x: number; y: number },
    robot: { x: number; y: number; h: number },
    prevH: number,
    stage: number,
    arcR: number
  ) {
    const sEnd = toScreen(end.x, end.y, field);
    const sInit = toScreen(init.x, init.y, field);
    const sCarrot = toScreen(carrot.x, carrot.y, field);
    const sGhost = toScreen(ghost.x, ghost.y, field);
    const sRobot = toScreen(robot.x, robot.y, field);

    // CROSS trip plane (⊥ the approach heading). The real line is infinite; draw a
    // 24"-wide stub (faded at the ends) so it reads as local to the target rather
    // than cutting the field in half. perpLine (= ec.d) slides it back toward
    // the robot along the direction of travel; negative means cross is off entirely.
    const arm = 12;
    const pX = Math.cos(targetH * D2R);
    const pY = -Math.sin(targetH * D2R);
    const slide = perpLine >= 0 ? (perpBack ? perpLine : -perpLine) : 0;
    const cX = end.x + Math.sin(targetH * D2R) * slide;
    const cY = end.y + Math.cos(targetH * D2R) * slide;
    const pe1 = toScreen(cX + pX * arm, cY + pY * arm, field);
    const pe2 = toScreen(cX - pX * arm, cY - pY * arm, field);
    const perpOn = perpLine >= 0;

    // instantaneous turn arc (skip when nearly straight)
    let arc: { cx: number; cy: number; r: number } | null = null;
    if (arcR > 0 && arcR < 160) {
      const dth = wrap(robot.h - prevH);
      const fh = robot.h * D2R;
      // right-hand normal of the forward vector (sin h, cos h)
      const rnx = Math.cos(fh);
      const rny = -Math.sin(fh);
      const sgn = dth >= 0 ? 1 : -1;
      const c = toScreen(robot.x + rnx * arcR * sgn, robot.y + rny * arcR * sgn, field);
      arc = { cx: c.sx, cy: c.sy, r: arcR };
    }

    return { sEnd, sInit, sCarrot, sGhost, sRobot, stage, pe1, pe2, perpOn, arc };
  }

  const geo = $derived.by(() => {
    if (override) {
      return buildGeo(
        override.end,
        override.targetH,
        override.perpLine,
        override.backward,
        override.init,
        override.carrot,
        override.ghost,
        override.robot,
        override.prevH,
        override.stage,
        override.arcR
      );
    }
    if (!movement) return null;
    const live = pair.cur;
    const isActive = movementAt(traceState.time)?.index === movement.index;
    if (!isActive || !live || live.mi !== movement.index || ticks.length === 0) return null;

    // s = 1 at the first tick, so its carrot IS the initial (un-retracted) carrot
    const initC = { x: ticks[0].carrotX, y: ticks[0].carrotY };
    return buildGeo(
      { x: movement.targetX, y: movement.targetY },
      movement.targetH,
      movement.exitPerpLine,
      movement.backwards,
      initC,
      { x: live.carrotX, y: live.carrotY },
      { x: live.ghostX, y: live.ghostY },
      { x: pose.x, y: pose.y, h: live.h },
      pair.prev ? pair.prev.h : live.h,
      live.stage,
      live.arcRadius
    );
  });
</script>

{#if geo}
  {#if geo.arc}
    <circle cx={geo.arc.cx} cy={geo.arc.cy} r={geo.arc.r} fill="none" stroke="#3b82f6" stroke-width="1" stroke-dasharray="2 3" opacity="0.3" vector-effect="non-scaling-stroke" />
  {/if}

  <!-- CROSS trip plane (omitted when d < 0 — cross is off) -->
  {#if geo.perpOn}
    <defs>
      <linearGradient id="boom-perp-fade" gradientUnits="userSpaceOnUse" x1={geo.pe1.sx} y1={geo.pe1.sy} x2={geo.pe2.sx} y2={geo.pe2.sy}>
        <stop offset="0" stop-color="#cfcfcf" stop-opacity="0" />
        <stop offset="0.28" stop-color="#cfcfcf" stop-opacity="0.3" />
        <stop offset="0.72" stop-color="#cfcfcf" stop-opacity="0.3" />
        <stop offset="1" stop-color="#cfcfcf" stop-opacity="0" />
      </linearGradient>
    </defs>
    <line x1={geo.pe1.sx} y1={geo.pe1.sy} x2={geo.pe2.sx} y2={geo.pe2.sy} stroke="url(#boom-perp-fade)" stroke-width="1" stroke-dasharray="3 3" vector-effect="non-scaling-stroke" />
  {/if}

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
