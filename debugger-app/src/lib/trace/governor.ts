// Shared helpers for the output-governor views (pipeline activation indicators
// + the inspector's Governors card). A "scope" is one of the three governor
// stages: the combined TotalOutput, or the drive / turn PID limiters.

import type { GovernConfig, GovernScope, Tick, TraceHeader } from './types';

export function scopeLabel(scope: GovernScope): string {
  if (scope === 'total') return 'Total output';
  if (scope === 'drive') return 'Drive PID';
  return 'Turn PID';
}

export function governConfig(header: TraceHeader, scope: GovernScope): GovernConfig {
  return header.govern[scope];
}

// The signal(s) each governor stage acts on. Total governs the two motor sides;
// the PID limiters act on their single channel output.
export function scopeSignals(t: Tick, scope: GovernScope): number[] {
  if (scope === 'total') return [t.leftOut, t.rightOut];
  if (scope === 'drive') return [t.driveOut];
  return [t.turnOut];
}

export interface GovernorFlags {
  min: boolean;
  max: boolean;
  accel: boolean;
  decel: boolean;
}

// Which governors are clamping at the current tick (derived from the output
// signal vs the configured limits, comparing to the previous tick for ramps).
export function activeGovernors(
  cur: Tick | null,
  prev: Tick | null,
  cfg: GovernConfig,
  scope: GovernScope
): GovernorFlags {
  const flags: GovernorFlags = { min: false, max: false, accel: false, decel: false };
  if (!cur) return flags;
  const vs = scopeSignals(cur, scope);
  const pvs = prev ? scopeSignals(prev, scope) : vs;
  for (let i = 0; i < vs.length; i++) {
    const v = vs[i];
    const pv = pvs[i] ?? v;
    const mag = Math.abs(v);
    if (cfg.maxOutput > 0 && mag >= cfg.maxOutput - 0.05) flags.max = true;
    if (cfg.minOutput > 0 && mag > 0.01 && mag <= cfg.minOutput + 0.05) flags.min = true;
    const dMag = mag - Math.abs(pv);
    if (cfg.maxAccelRate > 0 && dMag >= cfg.maxAccelRate - 0.01) flags.accel = true;
    if (cfg.maxDecelRate > 0 && -dMag >= cfg.maxDecelRate - 0.01) flags.decel = true;
  }
  return flags;
}

// Highest ramp rates actually experienced across a movement's ticks (V/tick),
// for comparison against the accel/decel limits.
export function experiencedRates(
  ticks: Tick[],
  scope: GovernScope
): { accel: number; decel: number } {
  let accel = 0;
  let decel = 0;
  for (let i = 1; i < ticks.length; i++) {
    const vs = scopeSignals(ticks[i], scope);
    const pvs = scopeSignals(ticks[i - 1], scope);
    for (let c = 0; c < vs.length; c++) {
      const dMag = Math.abs(vs[c]) - Math.abs(pvs[c]);
      if (dMag > accel) accel = dMag;
      if (-dMag > decel) decel = -dMag;
    }
  }
  return { accel, decel };
}
