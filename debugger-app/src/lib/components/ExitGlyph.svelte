<script lang="ts">
  // Exit-condition glyphs. Colorless (currentColor) so they never clash with
  // the movement colors. Category: settle | timeout | early | perp | schedule.
  let { category, size = 16 }: { category: string; size?: number } = $props();
</script>

{#if category === 'settle'}
  <!-- crosshairs -->
  <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round">
    <circle cx="12" cy="12" r="6.5" />
    <line x1="12" y1="1.5" x2="12" y2="5" />
    <line x1="12" y1="19" x2="12" y2="22.5" />
    <line x1="1.5" y1="12" x2="5" y2="12" />
    <line x1="19" y1="12" x2="22.5" y2="12" />
    <circle cx="12" cy="12" r="1.2" fill="currentColor" stroke="none" />
  </svg>
{:else if category === 'timeout'}
  <!-- hourglass -->
  <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
    <path d="M7 4 L17 4 L12 12 L17 20 L7 20 L12 12 Z" />
  </svg>
{:else if category === 'early'}
  <!-- early exit: door + arrow out (provisional) -->
  <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
    <path d="M13 4 H6 a2 2 0 0 0 -2 2 V18 a2 2 0 0 0 2 2 h7" />
    <path d="M9 12 H20" />
    <path d="M17 9 L20 12 L17 15" />
  </svg>
{:else if category === 'perp'}
  <!-- perp-line cross (provisional: arrow crossing a line) -->
  <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
    <line x1="14" y1="3.5" x2="14" y2="20.5" />
    <path d="M3.5 12 H20.5" />
    <path d="M17.5 9 L20.5 12 L17.5 15" />
  </svg>
{:else if category === 'schedule'}
  <!-- schedule: trajectory's reference clock ran out — a finish flag,
       distinct from settle's crosshairs (this is a clock reaching its end,
       not a controller deciding it's close enough) -->
  <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
    <line x1="6" y1="3" x2="6" y2="21" />
    <path d="M6 4.5 H17 L14 8.5 L17 12.5 H6" />
  </svg>
{/if}
