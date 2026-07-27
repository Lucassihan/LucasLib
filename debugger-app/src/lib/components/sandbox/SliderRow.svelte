<script lang="ts">
  // One left-panel parameter row: label · slider · click-to-type value.
  // Clicking the number swaps it for a text input (Enter/blur commits, Esc
  // cancels). Typed values clamp to `min`, and to `max` only when `capMax` —
  // elsewhere you can type past the slider's top end; the thumb just pins.
  //
  // slider={false} drops the track and keeps just the label + typed value, for
  // knobs where dragging makes no sense (an exit condition is a number you know,
  // not something you feel your way to) — `max` is then only the typing clamp.
  let {
    label,
    value,
    min,
    max,
    step,
    unit = '',
    decimals = 1,
    capMax = false,
    slider = true,
    onchange
  }: {
    label: string;
    value: number;
    min: number;
    max: number;
    step: number;
    unit?: string;
    decimals?: number;
    capMax?: boolean;
    slider?: boolean;
    onchange: (v: number) => void;
  } = $props();

  let editing = $state(false);
  let text = $state('');
  let inputEl: HTMLInputElement | undefined = $state();

  // trim zeros only after a decimal point (300 must stay 300)
  const fmt = (v: number) => {
    let s = v.toFixed(decimals);
    if (s.includes('.')) s = s.replace(/0+$/, '').replace(/\.$/, '');
    return s || '0';
  };

  function beginEdit() {
    text = fmt(value);
    editing = true;
    queueMicrotask(() => {
      inputEl?.focus();
      inputEl?.select();
    });
  }
  function commit() {
    if (!editing) return;
    editing = false;
    const n = parseFloat(text);
    if (Number.isNaN(n)) return;
    let v = Math.max(min, n);
    if (capMax) v = Math.min(max, v);
    onchange(v);
  }
</script>

<div class="row" class:bare={!slider}>
  <span class="rlbl">{label}</span>
  {#if slider}
    <input
      type="range"
      {min}
      {max}
      {step}
      value={Math.max(min, Math.min(max, value))}
      oninput={(e) => onchange(parseFloat((e.currentTarget as HTMLInputElement).value))}
    />
  {/if}
  {#if editing}
    <input
      bind:this={inputEl}
      class="rval-in"
      type="text"
      inputmode="decimal"
      bind:value={text}
      onblur={commit}
      onkeydown={(e) => {
        if (e.key === 'Enter') {
          e.preventDefault();
          commit();
        } else if (e.key === 'Escape') {
          editing = false;
        }
      }}
    />
  {:else}
    <button type="button" class="rval" onclick={beginEdit} title="Click to type a value">
      {fmt(value)}{unit ? ` ${unit}` : ''}
    </button>
  {/if}
</div>

<style>
  .row {
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .rlbl {
    font-size: 9.5px;
    color: var(--text-secondary);
    width: 108px;
    flex-shrink: 0;
  }
  /* no track: the label takes the freed space so the value stays right-aligned
     with the slider rows above it */
  .row.bare .rlbl {
    flex: 1;
    width: auto;
  }
  /* the value cell reads as an editable field here, not a passive readout */
  .row.bare .rval {
    box-shadow: var(--ring);
    background: var(--bg-base);
  }

  input[type='range'] {
    flex: 1;
    appearance: none;
    -webkit-appearance: none;
    height: 4px;
    border-radius: 2px;
    background: var(--bg-base);
    box-shadow: var(--ring);
    outline: none;
    cursor: pointer;
    min-width: 0;
  }
  input[type='range']::-webkit-slider-thumb {
    appearance: none;
    -webkit-appearance: none;
    width: 11px;
    height: 11px;
    border-radius: 50%;
    background: #cfcfcf;
    box-shadow: 0 1px 3px rgba(0, 0, 0, 0.6);
  }
  input[type='range']::-moz-range-thumb {
    width: 11px;
    height: 11px;
    border: none;
    border-radius: 50%;
    background: #cfcfcf;
    box-shadow: 0 1px 3px rgba(0, 0, 0, 0.6);
  }

  /* the value readout doubles as the click-to-type affordance */
  .rval {
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9px;
    font-weight: 600;
    color: var(--text-dim);
    width: 52px;
    flex-shrink: 0;
    text-align: right;
    font-variant-numeric: tabular-nums;
    background: none;
    border: none;
    padding: 1px 2px;
    border-radius: 3px;
    cursor: text;
    transition: color 0.15s ease, background 0.15s ease;
  }
  .rval:hover {
    color: var(--text-primary);
    background: rgba(255, 255, 255, 0.06);
  }
  .rval-in {
    width: 52px;
    flex-shrink: 0;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 9px;
    font-weight: 600;
    color: var(--text-primary);
    background: var(--bg-base);
    border: none;
    border-radius: 3px;
    box-shadow:
      var(--ring),
      0 0 0 1px rgba(255, 255, 255, 0.14);
    padding: 1px 3px;
    text-align: right;
    outline: none;
  }
</style>
