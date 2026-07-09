<script lang="ts">
  // Average drive-motor temperature (fl/ml/bl/fr/mr/br) at the scrub position.
  // Bands: ≤35 green · ≤40 yellow · 40–50 orange · ≥50 red.
  let { temp }: { temp: number } = $props();

  const color = $derived.by(() => {
    if (temp <= 35) return '#4ade80';
    if (temp <= 40) return '#facc15';
    if (temp < 50) return '#fb923c';
    return '#f87171';
  });
</script>

<div
  class="mtemp"
  style="--tcol: {color}"
  title="Drive motor temp — avg of fl · ml · bl · fr · mr · br"
  role="img"
  aria-label="Average drive motor temperature {Math.round(temp)} degrees"
>
  <svg viewBox="0 0 12 12" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round">
    <path d="M4.6 7.2 V2.4 a1.4 1.4 0 0 1 2.8 0 V7.2" />
    <circle cx="6" cy="8.8" r="2.1" />
    <line x1="6" y1="8.8" x2="6" y2="5.4" stroke-width="1.6" />
  </svg>
  <span class="val">{Math.round(temp)}<span class="u">°C</span></span>
</div>

<style>
  .mtemp {
    display: flex;
    align-items: center;
    gap: 3px;
    color: var(--tcol);
  }
  .mtemp svg {
    width: 12px;
    height: 12px;
    opacity: 0.9;
  }
  .val {
    font-size: 10px;
    font-weight: 600;
    font-variant-numeric: tabular-nums;
  }
  .u {
    font-size: 8px;
    font-weight: 400;
    opacity: 0.75;
  }
</style>
