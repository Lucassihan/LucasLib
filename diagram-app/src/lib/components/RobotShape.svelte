<script lang="ts">
  // Top-down chassis in its LOCAL frame (origin at center, front = up / −Y), no
  // transform. Mirrors the debugger's RobotPreview so wheel count / intake side /
  // size render identically. Per project rule: NO center heading line — the
  // intake (flexwheel) edge shows facing.
  import {
    chassisParts,
    holesToInches,
    trackingSnapYs,
    clampTrackingIndex
  } from '$lib/geom/chassis';
  import type { RobotChassis } from '$lib/model/types';

  let { chassis, color = '#1f2937', fill = true }: { chassis: RobotChassis; color?: string; fill?: boolean } =
    $props();

  const width = $derived(holesToInches(chassis.widthHoles));
  const length = $derived(holesToInches(chassis.lengthHoles));
  const parts = $derived(
    chassisParts(width, length, chassis.driveWheelCount, chassis.intakePosition, chassis.sixWheelLayout)
  );
  const trackY = $derived(
    trackingSnapYs(length, chassis.driveWheelCount, chassis.sixWheelLayout)[
      clampTrackingIndex(chassis.trackingCenterIndex, chassis.driveWheelCount)
    ] ?? 0
  );
</script>

<rect
  x={-width / 2}
  y={-length / 2}
  width={width}
  height={length}
  rx="1.5"
  fill={fill ? color : 'none'}
  fill-opacity={fill ? 0.12 : 0}
  stroke={color}
  stroke-width="0.5"
/>

{#each parts.wheels as w}
  <rect x={w.x} y={w.y} width={w.w} height={w.h} rx="0.5" fill="none" stroke={color} stroke-width="0.4" opacity="0.7" />
{/each}

{#each parts.flex as f}
  <rect x={f.x} y={f.y} width={f.w} height={f.h} rx="0.4" fill={color} opacity="0.9" />
{/each}

<circle cx="0" cy={trackY} r="1" fill={color} />
