<script lang="ts">
  // Live ramsete.moveTo geometry, drawn for the movement under the scrubber.
  //
  // Where LyPoseOverlay drew a FIXED line and the robot's perpendicular offset
  // from it, point-chase has no line to draw: the frame is rebuilt every tick
  // from the bearing to the target. What it has instead is a right triangle —
  // the robot's travel axis, the forward leg d·cos eθ along it, and the
  // cross-track leg e_y = d·sin eθ closing onto the target. That second leg is
  // the whole quantity the lyapunov term steers against.
  //
  // Signed d makes this direction-agnostic: on a backward move d is negative, so
  // the forward leg runs out of the robot's tail and the triangle still closes.
  //
  // CLOSE drops it all — the coupling is off — and shows the latched heading axis
  // the movement drives straight down instead.
  import { toScreen, defaultField, type FieldConfig } from '../field/coords';
  import type { Movement } from '$lib/trace/types';
  import { traceState, tickAt, movementAt, poseAt, movementTicks } from '$lib/stores/trace.svelte';
  import { crossTrackAt } from '$lib/trace/ramseteMove';

  let { movement, field = defaultField }: { movement: Movement; field?: FieldConfig } = $props();

  const D2R = Math.PI / 180;
  const GREEN = '#4ade80';
  const CROSS = '#fb923c'; // cross-track error — the thing the coupling corrects

  const live = $derived(tickAt(traceState.time));
  const pose = $derived(poseAt(traceState.time));
  const ticks = $derived(movementTicks(movement.index));

  const geo = $derived.by(() => {
    const isActive = movementAt(traceState.time)?.index === movement.index;
    if (!isActive || !live || live.mi !== movement.index) return null;

    const robot = toScreen(pose.x, pose.y, field);
    const target = toScreen(movement.targetX, movement.targetY, field);

    if (live.stage >= 1) {
      // CLOSE: heading and distance both latched at the stage flip — the movement
      // drives that axis for exactly that much and stops looking at the target.
      // Its lateralError on the flip tick IS the committed distance, so the axis
      // is drawn to its real end rather than as an open-ended ray.
      const flip = ticks.find((t) => t.stage >= 1);
      if (!flip) return null;
      const hr = flip.h * D2R;
      const len = flip.lateralError;
      const a = toScreen(flip.x, flip.y, field);
      const b = toScreen(flip.x + Math.sin(hr) * len, flip.y + Math.cos(hr) * len, field);
      return { close: true, robot, target, a, b, foot: robot };
    }

    // FAR: forward leg along the direction of travel, cross-track leg onto the target
    const eY = crossTrackAt(live);
    const forward = live.lateralError * Math.cos(live.headingError * D2R);
    const hr = pose.h * D2R;
    const footX = pose.x + Math.sin(hr) * forward;
    const footY = pose.y + Math.cos(hr) * forward;

    return {
      close: false,
      robot,
      target,
      foot: toScreen(footX, footY, field),
      eY,
      a: robot,
      b: target
    };
  });
</script>

{#if geo}
  {#if geo.close}
    <!-- latched heading axis the CLOSE stage drives straight down -->
    <line x1={geo.a.sx} y1={geo.a.sy} x2={geo.b.sx} y2={geo.b.sy} stroke={GREEN} stroke-width="1.2" stroke-dasharray="5 3" opacity="0.45" vector-effect="non-scaling-stroke" />
  {:else}
    <!-- bearing: robot -> target (the frame, rebuilt every tick) -->
    <line x1={geo.robot.sx} y1={geo.robot.sy} x2={geo.target.sx} y2={geo.target.sy} stroke={GREEN} stroke-width="1" stroke-dasharray="3 3" opacity="0.4" vector-effect="non-scaling-stroke" />

    <!-- forward leg: d·cos eθ along the direction of travel -->
    <line x1={geo.robot.sx} y1={geo.robot.sy} x2={geo.foot.sx} y2={geo.foot.sy} stroke={GREEN} stroke-width="1.2" opacity="0.7" vector-effect="non-scaling-stroke" />

    <!-- cross-track leg: e_y, closing the triangle onto the target -->
    <line x1={geo.foot.sx} y1={geo.foot.sy} x2={geo.target.sx} y2={geo.target.sy} stroke={CROSS} stroke-width="1.6" opacity="0.9" vector-effect="non-scaling-stroke" />
    <circle cx={geo.foot.sx} cy={geo.foot.sy} r="1.8" fill="none" stroke={GREEN} stroke-width="1.3" vector-effect="non-scaling-stroke" />
  {/if}
{/if}
