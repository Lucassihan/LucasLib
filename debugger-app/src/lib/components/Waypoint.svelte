<script lang="ts" module>
  // Per-instance gradient ids — the fade runs along each waypoint's own ⊥ line, so
  // the gradient is defined in user space and can't be shared between waypoints.
  let seq = 0;
  const uid = () => ++seq;
</script>

<script lang="ts">
  import { toScreen, defaultField, type FieldConfig } from '../field/coords';

  // A commanded target marker (dot) whose halo encodes the movement's spatial
  // exit conditions (matches checkLateralExit in the lib):
  //   - settleR -> the SETTLE activation circle: exit once inside it and stopped.
  //   - crossD  -> the CROSS trip line: an INFINITE line ⊥ the heading, slid crossD
  //                inches back toward the robot from the target. Exit the instant
  //                the robot is on the far side of it. crossD < 0 = cross is off.
  //   - otherwise -> a faint settle-tolerance circle (smallError).
  let {
    x,
    y,
    settleR = 0,
    crossD = -1,
    backward = false,
    smallError = 0,
    heading = 0,
    field = defaultField,
    onenter,
    onmove,
    onleave
  }: {
    x: number;
    y: number;
    settleR?: number;
    crossD?: number;
    /** the move ran in reverse — the line slides the other way (along travel) */
    backward?: boolean;
    smallError?: number;
    heading?: number;
    field?: FieldConfig;
    onenter?: (e: PointerEvent) => void;
    onmove?: (e: PointerEvent) => void;
    onleave?: (e: PointerEvent) => void;
  } = $props();

  const p = $derived(toScreen(x, y, field));

  const crossOn = $derived(crossD >= 0);

  // The real line is infinite, but drawing it wall-to-wall implies the robot has to
  // be near the target for it to matter. Draw a 24"-wide stub instead and fade both
  // ends out, so it reads as "this continues" without dominating the field.
  // ⊥ to forward (sin h, cos h) is (cos h, −sin h); the slide is along the heading
  // axis, backward from the target in the direction of TRAVEL — which is −heading
  // when the move runs in reverse.
  const ARM = 12; // half-width: 24" end to end
  const perp = $derived.by(() => {
    const hr = (heading * Math.PI) / 180;
    const px = Math.cos(hr); // along the perp line
    const py = -Math.sin(hr);
    const hx = Math.sin(hr); // heading axis (slide direction)
    const hy = Math.cos(hr);
    const line = (off: number) => {
      const cx = x + hx * off;
      const cy = y + hy * off;
      const a = toScreen(cx + px * ARM, cy + py * ARM, field);
      const b = toScreen(cx - px * ARM, cy - py * ARM, field);
      return { ax: a.sx, ay: a.sy, bx: b.sx, by: b.sy };
    };
    const slide = backward ? crossD : -crossD;
    return { atTarget: line(0), trip: line(slide) };
  });

  // One gradient serves BOTH lines: they're parallel and the same length, and the
  // crossD slide is along the heading axis — perpendicular to the gradient vector —
  // so it doesn't change how either line projects onto it.
  const gid = `wp-fade-${uid()}`;

  const fallbackR = $derived(settleR <= 0 && !crossOn ? smallError : 0);
  const hitR = $derived(Math.max(settleR, crossD, smallError, 4) + 2);
</script>

<g class="wp" onpointerenter={onenter} onpointermove={onmove} onpointerleave={onleave}>
  <!-- generous transparent hit area for hovering -->
  <circle cx={p.sx} cy={p.sy} r={hitR} fill="transparent" />

  {#if settleR > 0}
    <circle cx={p.sx} cy={p.sy} r={settleR} class="settle-zone" vector-effect="non-scaling-stroke" />
  {/if}

  {#if crossOn}
    <defs>
      <linearGradient
        id={gid}
        gradientUnits="userSpaceOnUse"
        x1={perp.atTarget.ax}
        y1={perp.atTarget.ay}
        x2={perp.atTarget.bx}
        y2={perp.atTarget.by}
      >
        <stop offset="0" class="fade-out" />
        <stop offset="0.28" class="fade-in" />
        <stop offset="0.72" class="fade-in" />
        <stop offset="1" class="fade-out" />
      </linearGradient>
    </defs>
    <!-- solid = the line that actually fires; faint = where it sits with crossD 0 -->
    {#if crossD > 0}
      <line
        x1={perp.atTarget.ax}
        y1={perp.atTarget.ay}
        x2={perp.atTarget.bx}
        y2={perp.atTarget.by}
        class="perp-band"
        stroke="url(#{gid})"
        vector-effect="non-scaling-stroke"
      />
    {/if}
    <line
      x1={perp.trip.ax}
      y1={perp.trip.ay}
      x2={perp.trip.bx}
      y2={perp.trip.by}
      class="perp"
      stroke="url(#{gid})"
      vector-effect="non-scaling-stroke"
    />
  {/if}

  {#if fallbackR > 0}
    <circle cx={p.sx} cy={p.sy} r={fallbackR} class="settle" vector-effect="non-scaling-stroke" />
  {/if}

  <circle cx={p.sx} cy={p.sy} r="1.6" class="dot" />
</g>

<style>
  .wp {
    cursor: pointer;
  }
  .dot {
    fill: var(--text-secondary);
  }
  /* settle activation circle — solid */
  .settle-zone {
    fill: none;
    stroke: var(--text-dim);
    stroke-width: 1.1;
  }
  /* the fade — stroke comes from the gradient, so the colour lives in the stops */
  .fade-in {
    stop-color: var(--text-secondary);
    stop-opacity: 1;
  }
  .fade-out {
    stop-color: var(--text-secondary);
    stop-opacity: 0;
  }
  .perp {
    stroke-width: 1.3;
  }
  /* where the line would sit with crossD = 0, when it's been slid off the target.
     Same gradient as the trip line, dimmed with opacity instead of a second colour */
  .perp-band {
    stroke-width: 1;
    stroke-dasharray: 2 3;
    stroke-opacity: 0.5;
  }
  .settle {
    fill: none;
    stroke: var(--text-ghost);
    stroke-width: 1;
  }
</style>
