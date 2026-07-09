<script lang="ts">
  import type { RobotChassis } from '$lib/model/types';
  import { holesToInches, clampTrackingIndex } from '$lib/geom/chassis';
  import { snapshot } from '$lib/model/scene.svelte';
  import RobotShape from './RobotShape.svelte';

  let { chassis, color = '#1f2937' }: { chassis: RobotChassis; color?: string } = $props();

  const width = $derived(holesToInches(chassis.widthHoles));
  const length = $derived(holesToInches(chassis.lengthHoles));
  const vb = $derived(`${-width / 2 - 3} ${-length / 2 - 3} ${width + 6} ${length + 6}`);
  const maxTrack = $derived(chassis.driveWheelCount - 2);

  let armed = true;
  function arm() {
    if (armed) {
      snapshot();
      armed = false;
    }
  }
  function disarm() {
    armed = true;
  }

  function setCount(v: number) {
    snapshot();
    chassis.driveWheelCount = v as 4 | 6 | 8;
    chassis.trackingCenterIndex = clampTrackingIndex(chassis.trackingCenterIndex, chassis.driveWheelCount);
  }
</script>

<div class="chassis">
  <div class="preview">
    <svg viewBox={vb} preserveAspectRatio="xMidYMid meet">
      <g transform="scale(1,-1)">
        <RobotShape {chassis} {color} fill={true} />
      </g>
    </svg>
  </div>

  <div class="fields">
    <label class="row">
      <span>Width (holes)</span>
      <input type="number" min="4" max="60" bind:value={chassis.widthHoles} onpointerdown={arm} onfocus={arm} onblur={disarm} onchange={disarm} />
    </label>
    <label class="row">
      <span>Length (holes)</span>
      <input type="number" min="4" max="60" bind:value={chassis.lengthHoles} onpointerdown={arm} onfocus={arm} onblur={disarm} onchange={disarm} />
    </label>
    <label class="row">
      <span>Drive wheels</span>
      <select value={chassis.driveWheelCount} onchange={(e) => setCount(+(e.target as HTMLSelectElement).value)}>
        <option value={4}>4</option>
        <option value={6}>6</option>
        <option value={8}>8</option>
      </select>
    </label>
    {#if chassis.driveWheelCount === 6}
      <label class="row">
        <span>6-wheel layout</span>
        <select bind:value={chassis.sixWheelLayout} onpointerdown={arm} onchange={disarm}>
          <option value="centered">Centered</option>
          <option value="gapFront">Gap front</option>
          <option value="gapBack">Gap back</option>
        </select>
      </label>
    {/if}
    <label class="row">
      <span>Intake side</span>
      <select bind:value={chassis.intakePosition} onpointerdown={arm} onchange={disarm}>
        <option value="front">Front</option>
        <option value="back">Back</option>
      </select>
    </label>
    <label class="row">
      <span>Track center</span>
      <input type="range" min="0" max={maxTrack} step="1" bind:value={chassis.trackingCenterIndex} onpointerdown={arm} onchange={disarm} />
    </label>
  </div>
</div>

<style>
  .chassis {
    display: flex;
    gap: 10px;
  }
  .preview {
    width: 74px;
    height: 74px;
    flex: none;
    background: #f4f5f3;
    border: 1px solid var(--border);
    border-radius: var(--radius-sm);
    padding: 4px;
  }
  .preview svg {
    width: 100%;
    height: 100%;
  }
  .fields {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 5px;
  }
</style>
