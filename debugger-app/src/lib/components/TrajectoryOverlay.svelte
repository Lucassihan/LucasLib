<script lang="ts">
  // ramsete.trajectory geometry: the baked schedule's shape, the reference pose
  // riding along it, and the gap the law is closing.
  //
  // The driven path is already on the field (Path.svelte), speed-colored. What
  // was missing is the thing it is being compared against — RAMSETE steers to
  // close the gap between the robot and this ghost, so the gap IS the error the
  // law is working on. Seeing both makes a lagging clock or a cross-track offset
  // obvious in a way the pose alone never is.
  //
  // Two curves, and the difference between them is the point. The wide one is
  // the plan, straight out of the [SCHEDULE] block: every baked row, whether or
  // not the run ever got there. The narrow one is the reference actually served
  // per tick — it stops where the clock stopped and bunches up wherever the
  // clock was braked. A short trail on a long plan is a run that never finished
  // its schedule.
  //
  // Drawn in two layers so the field can put the curves UNDER the driven path
  // (the plan is a backdrop, the run is the subject) while the ghost robot and
  // the error line stay on top where they can be read.
  import Robot from './Robot.svelte';
  import { toScreen, defaultField, type FieldConfig } from '../field/coords';
  import type { Movement } from '$lib/trace/types';
  import { traceState, tickAt, movementAt, movementTicks } from '$lib/stores/trace.svelte';
  import { settings, holesToInches } from '$lib/stores/settings.svelte';

  let {
    movement,
    field = defaultField,
    layer = 'ghost'
  }: { movement: Movement; field?: FieldConfig; layer?: 'paths' | 'ghost' } = $props();

  const REF = '#a78bfa'; // the schedule — same violet the tuner uses for kA

  const live = $derived(tickAt(traceState.time));
  const ticks = $derived(movementTicks(movement.index));
  const isActive = $derived(movementAt(traceState.time)?.index === movement.index);

  // Traces taken before the refX/refY columns existed parse as 0, which would
  // draw a ghost sitting at the field origin. Treat an all-zero reference as
  // "this trace predates the feature" and draw nothing.
  const hasRef = $derived(ticks.some((t) => t.refX !== 0 || t.refY !== 0));

  // Absent on traces recorded before [SCHEDULE] existed — those still get the
  // served trail, just without the plan behind it.
  const schedule = $derived(traceState.trace.schedules[movement.index] ?? []);

  const points = (pts: { x: number; y: number }[]) =>
    pts
      .map((p) => toScreen(p.x, p.y, field))
      .map((p) => `${p.sx},${p.sy}`)
      .join(' ');

  const planPath = $derived(schedule.length > 1 ? points(schedule) : '');

  // Ticks the follow loop didn't publish a reference for — the stop command
  // after the loop is one — read as an exact (0, 0, 0), which would drag the
  // trail to the field origin. Old traces still carry them; the library holds
  // the last reference through that tick now.
  const served = $derived(ticks.filter((t) => t.refX !== 0 || t.refY !== 0 || t.refH !== 0));

  const refPath = $derived(hasRef ? points(served.map((t) => ({ x: t.refX, y: t.refY }))) : '');

  // One line, not two. The baked schedule IS the reference, complete and evenly
  // spaced; the per-tick trail is the same curve resampled by a clock that
  // stalls, so drawing both just thickened the same shape. The trail is the
  // fallback for traces recorded before [SCHEDULE] existed.
  const referencePath = $derived(planPath || refPath);

  // Where the schedule expects the robot to be standing when it starts. Drawn on
  // its own because a run whose first row is nowhere near the start pose is the
  // single most expensive mistake this overlay can catch.
  const origin = $derived(schedule.length ? toScreen(schedule[0].x, schedule[0].y, field) : null);

  const ghost = $derived(
    hasRef && isActive && live && live.mi === movement.index
      ? { x: live.refX, y: live.refY, h: live.refH }
      : null
  );
</script>

{#if layer === 'paths'}
  {#if referencePath}
    <polyline
      points={referencePath}
      fill="none"
      stroke={REF}
      stroke-width="1.5"
      stroke-linejoin="round"
      stroke-linecap="round"
      vector-effect="non-scaling-stroke"
    />
  {/if}

  {#if origin}
    <circle cx={origin.sx} cy={origin.sy} r="1.6" fill="none" stroke={REF} stroke-width="0.5" opacity="0.7" />
  {/if}
{:else if ghost}
  <g class="ghost">
    <Robot
      x={ghost.x}
      y={ghost.y}
      h={ghost.h}
      width={holesToInches(settings.robotWidthHoles)}
      length={holesToInches(settings.robotLengthHoles)}
      {field}
    />
  </g>

  <!-- the gap the law is closing -->
  {#if live}
    {@const a = toScreen(live.x, live.y, field)}
    {@const b = toScreen(ghost.x, ghost.y, field)}
    <line x1={a.sx} y1={a.sy} x2={b.sx} y2={b.sy} stroke={REF} stroke-width="0.5" opacity="0.6" />
  {/if}
{/if}

<style>
  /* the reference is a phantom, not a second robot competing for attention */
  .ghost {
    opacity: 0.42;
    color: #a78bfa;
    pointer-events: none;
  }
</style>
