<script lang="ts">
  import { settings } from '$lib/stores/settings.svelte';

  // Four workspaces: Replay (trace playback), Sandbox (route writing), the
  // Trajectory Generator, and the Velocity Controller worksheet. The right slot
  // shows the loaded trace's auton name in replay mode.
  let {
    status = 'No trace loaded',
    onOpenSettings,
    onOpenLibrary
  }: {
    status?: string;
    onOpenSettings?: () => void;
    onOpenLibrary?: () => void;
  } = $props();
</script>

<header class="topbar">
  <span class="title">LucasLib Debugger</span>

  <div class="mode" role="tablist" aria-label="Workspace">
    <button
      class="mode-btn"
      class:on={settings.appMode === 'replay'}
      role="tab"
      aria-selected={settings.appMode === 'replay'}
      onclick={() => (settings.appMode = 'replay')}>Replay</button
    >
    <button
      class="mode-btn"
      class:on={settings.appMode === 'sandbox'}
      role="tab"
      aria-selected={settings.appMode === 'sandbox'}
      onclick={() => (settings.appMode = 'sandbox')}>Sandbox</button
    >
    <button
      class="mode-btn"
      class:on={settings.appMode === 'trajectory'}
      role="tab"
      aria-selected={settings.appMode === 'trajectory'}
      onclick={() => (settings.appMode = 'trajectory')}>Trajectory Generator</button
    >
    <button
      class="mode-btn"
      class:on={settings.appMode === 'velocity'}
      role="tab"
      aria-selected={settings.appMode === 'velocity'}
      onclick={() => (settings.appMode = 'velocity')}>Velocity Controller</button
    >
  </div>

  <div class="right">
    {#if settings.appMode === 'replay'}
      <button class="status-btn" onclick={onOpenLibrary} aria-label="Open library">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
          <path d="M3 7h18M3 12h18M3 17h18" />
        </svg>
        <span class="status">{status}</span>
      </button>
    {/if}
    <button class="gear" onclick={onOpenSettings} aria-label="Settings">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <circle cx="12" cy="12" r="3" />
        <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z" />
      </svg>
    </button>
  </div>
</header>

<style>
  .topbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 2px 6px;
  }

  /* App chrome: whisper-quiet, low weight */
  .title {
    font-size: 11px;
    font-weight: 300;
    color: var(--text-dim);
    letter-spacing: 0.02em;
  }

  .mode {
    display: flex;
    gap: 2px;
    background: var(--bg-base);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 2px;
  }
  .mode-btn {
    border: none;
    background: none;
    color: var(--text-dim);
    font-family: inherit;
    font-size: 9.5px;
    font-weight: 600;
    padding: 3px 12px;
    border-radius: 4px;
    cursor: pointer;
    transition: background 0.15s ease, color 0.15s ease;
  }
  .mode-btn:hover {
    color: var(--text-primary);
  }
  .mode-btn.on {
    background: rgba(255, 255, 255, 0.1);
    color: var(--text-primary);
  }

  .right {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .status-btn {
    display: flex;
    align-items: center;
    gap: 6px;
    background: none;
    border: none;
    padding: 3px 8px;
    color: var(--text-muted);
    cursor: pointer;
    border-radius: var(--radius-small);
    transition: background 0.15s ease, color 0.15s ease;
    font-family: inherit;
  }
  .status-btn:hover {
    background: var(--bg-base);
    color: var(--text-primary);
  }
  .status-btn svg {
    width: 12px;
    height: 12px;
    opacity: 0.7;
  }

  .status {
    font-size: 10px;
    font-weight: 400;
    color: inherit;
  }

  .gear {
    display: flex;
    align-items: center;
    justify-content: center;
    background: none;
    border: none;
    padding: 2px;
    color: var(--text-muted);
    cursor: pointer;
    transition: color 0.15s ease;
  }
  .gear:hover {
    color: var(--text-primary);
  }
  .gear svg {
    width: 15px;
    height: 15px;
  }
</style>
