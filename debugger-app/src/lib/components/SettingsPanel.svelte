<script lang="ts">
  import { settings, holesToInches, type DriveWheelCount } from '$lib/stores/settings.svelte';
  import RobotPreview from './RobotPreview.svelte';

  let { open = $bindable() }: { open: boolean } = $props();

  const wheelOptions: DriveWheelCount[] = [4, 6, 8];

  function close() {
    open = false;
  }

  function onKey(e: KeyboardEvent) {
    if (e.key === 'Escape') close();
  }
</script>

<svelte:window onkeydown={onKey} />

{#if open}
  <div class="backdrop" onclick={close} role="presentation">
    <div class="panel" onclick={(e) => e.stopPropagation()} role="dialog" aria-label="Settings">
      <div class="head">
        <span class="title">Settings</span>
        <button class="close" onclick={close} aria-label="Close settings">×</button>
      </div>

      <div class="preview">
        <RobotPreview />
      </div>

      <section class="group">
        <div class="group-label">Robot Size</div>

        <div class="row">
          <label for="rw">Width</label>
          <input id="rw" type="number" min="1" step="1" bind:value={settings.robotWidthHoles} />
          <span class="unit">holes</span>
          <span class="conv">= {holesToInches(settings.robotWidthHoles).toFixed(1)} in</span>
        </div>

        <div class="row">
          <label for="rl">Length</label>
          <input id="rl" type="number" min="1" step="1" bind:value={settings.robotLengthHoles} />
          <span class="unit">holes</span>
          <span class="conv">= {holesToInches(settings.robotLengthHoles).toFixed(1)} in</span>
        </div>

        <p class="note">1 hole = 0.5 in (VEX hole spacing). Chassis are counted in holes.</p>
      </section>

      <section class="group">
        <div class="group-label">Drivetrain</div>

        <div class="row seg-row">
          <span class="seg-label">Wheels</span>
          <div class="seg">
            {#each wheelOptions as n}
              <button
                class="seg-btn"
                class:active={settings.driveWheelCount === n}
                onclick={() => (settings.driveWheelCount = n)}
              >{n}</button>
            {/each}
          </div>
        </div>

        <div class="row seg-row">
          <span class="seg-label">Intake</span>
          <div class="seg">
            <button
              class="seg-btn"
              class:active={settings.intakePosition === 'front'}
              onclick={() => (settings.intakePosition = 'front')}
            >Front</button>
            <button
              class="seg-btn"
              class:active={settings.intakePosition === 'back'}
              onclick={() => (settings.intakePosition = 'back')}
            >Back</button>
          </div>
        </div>

        <p class="note">Wheels spread to the widest wheelbase; flexwheels sit on the intake edge.</p>
      </section>
    </div>
  </div>
{/if}

<style>
  .backdrop {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.55);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 100;
  }

  .panel {
    width: 340px;
    background: var(--bg-elevated);
    border-radius: var(--radius-panel);
    box-shadow:
      var(--ring),
      0 6px 24px rgba(0, 0, 0, 0.4);
    padding: 18px;
  }

  .head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 16px;
  }

  .title {
    font-size: 14px;
    font-weight: 700;
    color: var(--text-emphasis);
  }

  .close {
    background: none;
    border: none;
    font-family: inherit;
    font-size: 18px;
    line-height: 1;
    color: var(--text-dim);
    cursor: pointer;
    padding: 0 2px;
    transition: color 0.15s ease;
  }
  .close:hover {
    color: var(--text-primary);
  }

  .preview {
    height: 150px;
    display: flex;
    align-items: center;
    justify-content: center;
    background: var(--bg-deep);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 10px;
    margin-bottom: 18px;
  }

  .group-label {
    font-size: 10px;
    font-weight: 600;
    color: var(--text-muted);
    margin-bottom: 10px;
  }

  .row {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 8px;
  }

  label {
    width: 52px;
    font-size: 11px;
    font-weight: 400;
    color: var(--text-secondary);
  }

  input {
    width: 60px;
    background: var(--bg-raised);
    color: var(--text-primary);
    border: none;
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 5px 8px;
    font-family: inherit;
    font-size: 12px;
    font-weight: 600;
    outline: none;
  }
  input:focus {
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.12);
  }

  .unit {
    font-size: 10px;
    font-weight: 400;
    color: var(--text-dim);
  }

  .conv {
    margin-left: auto;
    font-size: 11px;
    font-weight: 600;
    color: var(--text-secondary);
  }

  .note {
    margin-top: 12px;
    font-size: 9px;
    font-weight: 400;
    color: var(--text-ghost);
    line-height: 1.4;
  }

  .seg-row {
    justify-content: flex-start;
  }
  .seg-label {
    width: 52px;
    font-size: 11px;
    font-weight: 400;
    color: var(--text-secondary);
  }
  .seg {
    display: flex;
    gap: 4px;
  }
  .seg-btn {
    background: var(--bg-raised);
    color: var(--text-secondary);
    border: none;
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 5px 12px;
    font-family: inherit;
    font-size: 12px;
    font-weight: 600;
    cursor: pointer;
    transition: background 0.15s ease, color 0.15s ease;
  }
  .seg-btn:hover {
    color: var(--text-primary);
  }
  .seg-btn.active {
    background: rgba(255, 255, 255, 0.12);
    color: var(--text-emphasis);
    box-shadow: inset 0 0 0 1px rgba(255, 255, 255, 0.18);
  }
</style>
