<script lang="ts">
  // Live lyPose (path-follow Lyapunov) geometry, shown for the lyPose movement
  // under the scrubber. Draws the FIXED path line the robot rides (through the
  // target at pathHeading), the target pose + final-heading arrow, and the
  // robot's cross-track error e_y (perpendicular from the robot to its foot
  // point on the line). Matches lyapunovCalculate's frame exactly.
  import { toScreen, defaultField, type FieldConfig } from '../field/coords';
  import type { Movement } from '$lib/trace/types';
  import { traceState, tickAt, movementAt, poseAt } from '$lib/stores/trace.svelte';

  let { movement, field = defaultField }: { movement: Movement; field?: FieldConfig } = $props();

  const D2R = Math.PI / 180;
  const TEAL = '#2dd4bf';
  const CROSS = '#fb923c'; // cross-track error — the thing being corrected

  const live = $derived(tickAt(traceState.time));
  const pose = $derived(poseAt(traceState.time));

  const geo = $derived.by(() => {
    const isActive = movementAt(traceState.time)?.index === movement.index;
    if (!isActive || !live || live.mi !== movement.index) return null;

    const ph = movement.pathHeading;
    const dir = { x: Math.sin(ph * D2R), y: Math.cos(ph * D2R) }; // along the line
    const tx = movement.targetX;
    const ty = movement.targetY;

    // foot point: robot projected onto the line through the target
    const along = (pose.x - tx) * dir.x + (pose.y - ty) * dir.y;
    const footX = tx + along * dir.x;
    const footY = ty + along * dir.y;

    // The path line runs from behind the target (−dir, where the robot
    // approaches) UP TO the target — it does not extend past the pose.
    const a = toScreen(tx - dir.x * 120, ty - dir.y * 120, field);
    const end = toScreen(tx, ty, field);
    const foot = toScreen(footX, footY, field);
    const robot = toScreen(pose.x, pose.y, field);

    return { a, end, foot, robot };
  });
</script>

{#if geo}
  <!-- the fixed path line the robot rides, ending at the target pose -->
  <line x1={geo.a.sx} y1={geo.a.sy} x2={geo.end.sx} y2={geo.end.sy} stroke={TEAL} stroke-width="1.2" stroke-dasharray="5 3" opacity="0.55" vector-effect="non-scaling-stroke" />

  <!-- cross-track error: robot -> foot point on the line -->
  <line x1={geo.robot.sx} y1={geo.robot.sy} x2={geo.foot.sx} y2={geo.foot.sy} stroke={CROSS} stroke-width="1.6" opacity="0.9" vector-effect="non-scaling-stroke" />
  <circle cx={geo.foot.sx} cy={geo.foot.sy} r="1.8" fill="none" stroke={TEAL} stroke-width="1.3" vector-effect="non-scaling-stroke" />

  <!-- target pose -->
  <circle cx={geo.end.sx} cy={geo.end.sy} r="2.8" fill={TEAL} />
{/if}
