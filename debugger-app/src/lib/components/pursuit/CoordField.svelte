<script lang="ts">
  // An always-editable type-in tile (uppercase label · mono value · gold focus
  // underline) — for exact anchor coordinates, matching the drivetrain spec
  // tiles. Enter/blur commits, Esc reverts; external changes show while unfocused.
  let {
    label,
    value,
    unit = '',
    decimals = 1,
    onchange
  }: {
    label: string;
    value: number;
    unit?: string;
    decimals?: number;
    onchange: (v: number) => void;
  } = $props();

  let focused = $state(false);
  let text = $state('');

  const fmt = (v: number) => {
    let s = v.toFixed(decimals);
    if (s.includes('.')) s = s.replace(/0+$/, '').replace(/\.$/, '');
    return s || '0';
  };

  const shown = $derived(focused ? text : fmt(value));

  function onFocus(e: FocusEvent) {
    focused = true;
    text = fmt(value);
    (e.currentTarget as HTMLInputElement).select();
  }
  function commit() {
    const n = parseFloat(text);
    if (!Number.isNaN(n)) onchange(n);
    focused = false;
  }
</script>

<label class="cf">
  <span class="cf-lbl">{label}</span>
  <span class="cf-box">
    <input
      class="cf-in"
      type="text"
      inputmode="decimal"
      value={shown}
      onfocus={onFocus}
      oninput={(e) => (text = e.currentTarget.value)}
      onblur={commit}
      onkeydown={(e) => {
        if (e.key === 'Enter') {
          e.preventDefault();
          e.currentTarget.blur();
        } else if (e.key === 'Escape') {
          focused = false;
          e.currentTarget.blur();
        }
      }}
    />
    {#if unit}<span class="cf-unit">{unit}</span>{/if}
  </span>
</label>

<style>
  .cf {
    display: flex;
    flex-direction: column;
    gap: 3px;
    min-width: 0;
  }
  .cf-lbl {
    font-size: 8.5px;
    font-weight: 700;
    letter-spacing: 0.05em;
    text-transform: uppercase;
    color: var(--text-ghost);
  }
  .cf-box {
    display: flex;
    align-items: baseline;
    gap: 3px;
    background: var(--bg-base);
    border-radius: var(--radius-small);
    box-shadow: var(--ring);
    padding: 6px 8px;
    border-bottom: 1.5px solid transparent;
    transition: border-color 0.15s ease;
  }
  .cf-box:focus-within {
    border-bottom-color: #fbbf24;
  }
  .cf-in {
    flex: 1;
    min-width: 0;
    width: 100%;
    background: none;
    border: none;
    outline: none;
    font-family: ui-monospace, 'SF Mono', Menlo, monospace;
    font-size: 14px;
    font-weight: 700;
    color: var(--text-primary);
    font-variant-numeric: tabular-nums;
    padding: 0;
  }
  .cf-unit {
    font-size: 9px;
    font-weight: 400;
    color: var(--text-dim);
    flex-shrink: 0;
  }
</style>
