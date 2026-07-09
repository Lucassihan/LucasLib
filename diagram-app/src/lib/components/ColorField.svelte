<script lang="ts">
  import { snapshot } from '$lib/model/scene.svelte';

  let {
    value = $bindable('#1f2937'),
    allowNone = false
  }: { value?: string; allowNone?: boolean } = $props();

  const swatches = ['#1f2937', '#2563eb', '#ef4444', '#16a34a', '#f59e0b', '#a855f7', '#0891b2', '#ffffff'];

  function pick(c: string) {
    snapshot();
    value = c;
  }
</script>

<div class="cf">
  {#if allowNone}
    <button class="sw none" class:active={value === 'none'} title="No fill" onclick={() => pick('none')}></button>
  {/if}
  {#each swatches as c}
    <button class="sw" class:active={value === c} style="background:{c}" title={c} onclick={() => pick(c)} aria-label={c}></button>
  {/each}
  <input
    type="color"
    value={value === 'none' ? '#000000' : value}
    onpointerdown={() => snapshot()}
    oninput={(e) => (value = (e.target as HTMLInputElement).value)}
  />
</div>

<style>
  .cf {
    display: flex;
    flex-wrap: wrap;
    gap: 4px;
    align-items: center;
  }
  .sw {
    width: 18px;
    height: 18px;
    border-radius: 4px;
    border: 1px solid var(--border-strong);
    padding: 0;
  }
  .sw.active {
    outline: 2px solid var(--accent);
    outline-offset: 1px;
  }
  .sw.none {
    background: repeating-conic-gradient(#bbb 0% 25%, #fff 0% 50%) 50% / 8px 8px;
    position: relative;
  }
  input[type='color'] {
    width: 26px;
    height: 22px;
    padding: 0;
    border: 1px solid var(--border-strong);
    border-radius: 4px;
    background: none;
  }
</style>
