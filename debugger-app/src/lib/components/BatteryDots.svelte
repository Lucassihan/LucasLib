<script lang="ts">
  // VEX-style battery indicator: 4 dots read left to right, each worth 25%
  // (75% = three lit, one dark). Shows the level captured at debugger::start().
  let { pct, volts = 0 }: { pct: number; volts?: number } = $props();

  const lit = $derived(Math.max(pct > 0 ? 1 : 0, Math.min(4, Math.round(pct / 25))));
  const low = $derived(lit <= 1);
</script>

<div
  class="battery"
  title={`Battery at the playhead${volts > 0 ? ` · ${volts.toFixed(1)} V at start` : ''}`}
  role="img"
  aria-label="Battery {Math.round(pct)} percent"
>
  {#each Array(4) as _, i}
    <span class="dot" class:lit={i < lit} class:low></span>
  {/each}
  <span class="pct">{Math.round(pct)}%</span>
</div>

<style>
  .battery {
    display: flex;
    align-items: center;
    gap: 5px;
  }

  /* LED look: unlit is a recessed socket. Lit is glassy, not neon — a dark
     translucent outer disc with a faint rim, and a deeper concentric core
     floating inside it (the two shades carry the darkness). */
  .dot {
    position: relative;
    width: 9px;
    height: 9px;
    border-radius: 50%;
    background: radial-gradient(circle at 35% 30%, #1d1d1d, #0c0c0c 72%);
    box-shadow:
      inset 0 1.5px 3px rgba(0, 0, 0, 0.8),
      inset 0 -1px 1px rgba(255, 255, 255, 0.04),
      0 1px 0 rgba(255, 255, 255, 0.04);
  }
  /* lit = light shining through dark translucent plastic: a bright pinpoint
     core that falls off fast into deep green, under a glassy top glint */
  .dot.lit {
    background: rgba(16, 74, 40, 0.42);
    box-shadow:
      inset 0 0 0 1px rgba(74, 222, 128, 0.2),
      inset 0 1px 2px rgba(0, 0, 0, 0.55),
      0 0 6px rgba(74, 222, 128, 0.24);
  }
  .dot.lit::after {
    content: '';
    position: absolute;
    inset: 2px;
    border-radius: 50%;
    background:
      radial-gradient(circle at 32% 24%, rgba(255, 255, 255, 0.5), rgba(255, 255, 255, 0) 34%),
      radial-gradient(
        circle at 50% 48%,
        rgba(226, 255, 238, 0.95),
        rgba(110, 231, 163, 0.6) 24%,
        rgba(21, 94, 50, 0.6) 62%,
        rgba(6, 44, 24, 0.78)
      );
    box-shadow: inset 0 -1px 1.5px rgba(0, 0, 0, 0.45);
  }
  .dot.lit.low {
    background: rgba(84, 20, 20, 0.42);
    box-shadow:
      inset 0 0 0 1px rgba(248, 113, 113, 0.2),
      inset 0 1px 2px rgba(0, 0, 0, 0.55),
      0 0 6px rgba(248, 113, 113, 0.26);
  }
  .dot.lit.low::after {
    background:
      radial-gradient(circle at 32% 24%, rgba(255, 255, 255, 0.45), rgba(255, 255, 255, 0) 34%),
      radial-gradient(
        circle at 50% 48%,
        rgba(255, 238, 238, 0.95),
        rgba(248, 113, 113, 0.55) 24%,
        rgba(127, 29, 29, 0.6) 62%,
        rgba(56, 12, 12, 0.8)
      );
  }

  .pct {
    margin-left: 3px;
    font-size: 10px;
    font-weight: 600;
    color: var(--text-dim);
    font-variant-numeric: tabular-nums;
  }
</style>
