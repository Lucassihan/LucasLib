// fit.ts — the two regressions behind the Velocity Controller tab.
//
// Both come straight from velocityController.h's measurement procedure:
//
//   1. a held-voltage sweep fits   volts = kV·v + kS·sign(v)      (linear)
//   2. one step response fits      v = K·u·(1 − e^−(t−L)/T)       (nonlinear)
//
// The second is a three-parameter FOPDT (first-order plus dead time) model, so
// it needs an iterative solver — Levenberg–Marquardt over [K, L, T].

export interface Pt {
  x: number;
  y: number;
}

/**
 * Pull (x, y) pairs out of free-form text. Accepts Desmos list syntax
 * (`\left(1,0.39\right)`), plain parens, CSV, or whitespace columns — every
 * number in reading order, paired up two at a time.
 */
export function parsePairs(text: string): Pt[] {
  const nums = text.match(/-?\d*\.?\d+(?:[eE][-+]?\d+)?/g);
  if (!nums) return [];
  const out: Pt[] = [];
  for (let i = 0; i + 1 < nums.length; i += 2) {
    const x = Number(nums[i]);
    const y = Number(nums[i + 1]);
    if (Number.isFinite(x) && Number.isFinite(y)) out.push({ x, y });
  }
  return out;
}

function sign(v: number): number {
  return v > 0 ? 1 : v < 0 ? -1 : 0;
}

/** Coefficient of determination of `pred` against the observed `obs`. */
function rSquared(obs: number[], pred: number[]): number {
  const n = obs.length;
  if (n < 2) return 0;
  const mean = obs.reduce((a, b) => a + b, 0) / n;
  let ssRes = 0;
  let ssTot = 0;
  for (let i = 0; i < n; i++) {
    ssRes += (obs[i] - pred[i]) ** 2;
    ssTot += (obs[i] - mean) ** 2;
  }
  return ssTot > 0 ? 1 - ssRes / ssTot : 0;
}

// ---------------------------------------------------------------------------
// Step 1 — steady-state sweep
// ---------------------------------------------------------------------------

export interface SteadyFit {
  kV: number;
  kS: number;
  r2: number;
  n: number;
  ok: boolean;
}

/**
 * Least squares on volts = kV·v + kS·sign(v). Points are (motor input, measured
 * velocity) — the same order the data is collected in — so velocity is the
 * regressor and voltage is the response.
 */
export function fitSteady(pts: Pt[]): SteadyFit {
  const data = pts.filter((p) => Number.isFinite(p.x) && Number.isFinite(p.y));
  if (data.length < 2) return { kV: 0, kS: 0, r2: 0, n: data.length, ok: false };

  // normal equations for the 2-column design matrix [v, sign(v)]
  let a11 = 0;
  let a12 = 0;
  let a22 = 0;
  let b1 = 0;
  let b2 = 0;
  for (const p of data) {
    const v = p.y;
    const s = sign(v);
    a11 += v * v;
    a12 += v * s;
    a22 += s * s;
    b1 += v * p.x;
    b2 += s * p.x;
  }

  const det = a11 * a22 - a12 * a12;
  let kV: number;
  let kS: number;
  if (Math.abs(det) < 1e-12) {
    // every sample the same sign AND degenerate — fall back to a line through 0
    kV = a11 > 0 ? b1 / a11 : 0;
    kS = 0;
  } else {
    kV = (b1 * a22 - b2 * a12) / det;
    kS = (a11 * b2 - a12 * b1) / det;
  }

  const pred = data.map((p) => kV * p.y + kS * sign(p.y));
  return {
    kV,
    kS,
    r2: rSquared(data.map((p) => p.x), pred),
    n: data.length,
    ok: true
  };
}

/** volts predicted for a velocity, from a steady-state fit. */
export function steadyAt(fit: SteadyFit, v: number): number {
  return fit.kV * v + fit.kS * sign(v);
}

// ---------------------------------------------------------------------------
// Step 2 — step response (FOPDT)
// ---------------------------------------------------------------------------

export interface FopdtFit {
  K: number;
  L: number;
  T: number;
  r2: number;
  n: number;
  iterations: number;
  ok: boolean;
}

export function fopdtAt(fit: FopdtFit, uStep: number, t: number): number {
  return fit.K * uStep * (1 - Math.exp(-(t - fit.L) / fit.T));
}

function fopdtCost(pts: Pt[], u: number, p: number[]): number {
  const [K, L, T] = p;
  let sum = 0;
  for (const pt of pts) {
    const r = K * u * (1 - Math.exp(-(pt.x - L) / T)) - pt.y;
    sum += r * r;
  }
  return sum;
}

/** Gaussian elimination on a small dense system. Returns null if singular. */
function solve(A: number[][], b: number[]): number[] | null {
  const n = b.length;
  const M = A.map((row, i) => [...row, b[i]]);
  for (let c = 0; c < n; c++) {
    let piv = c;
    for (let r = c + 1; r < n; r++) if (Math.abs(M[r][c]) > Math.abs(M[piv][c])) piv = r;
    if (Math.abs(M[piv][c]) < 1e-14) return null;
    [M[c], M[piv]] = [M[piv], M[c]];
    for (let r = 0; r < n; r++) {
      if (r === c) continue;
      const f = M[r][c] / M[c][c];
      for (let k = c; k <= n; k++) M[r][k] -= f * M[c][k];
    }
  }
  return M.map((row, i) => row[n] / M[i][i]);
}

/**
 * Fit v = K·u·(1 − e^−(t−L)/T) over the whole capture by Levenberg–Marquardt.
 *
 * Every sample is weighted equally, including the ones before the dead time
 * where the model predicts a negative velocity — that is what makes L identify
 * at all, and it is what Desmos' regression does.
 */
export function fitFopdt(pts: Pt[], uStep: number): FopdtFit {
  const data = pts.filter((p) => Number.isFinite(p.x) && Number.isFinite(p.y));
  const fail = { K: 0, L: 0, T: 0, r2: 0, n: data.length, iterations: 0, ok: false };
  if (data.length < 4 || !(Math.abs(uStep) > 1e-9)) return fail;

  // seed from the shape of the capture: steady state is the tail average, and
  // the time constant is where the rise first crosses 63.2% of it
  const tail = data.slice(Math.floor(data.length * 0.8));
  const vss = tail.reduce((a, p) => a + p.y, 0) / tail.length;
  if (!(Math.abs(vss) > 1e-9)) return fail;

  const tSpan = data[data.length - 1].x - data[0].x;
  const cross = (frac: number) => {
    const hit = data.find((p) => Math.abs(p.y) >= Math.abs(vss) * frac);
    return hit ? hit.x - data[0].x : tSpan * frac;
  };
  const L0 = Math.max(0, cross(0.05));
  const T0 = Math.max(cross(0.632) - L0, tSpan * 0.02);

  let p = [vss / uStep, L0, T0];
  let cost = fopdtCost(data, uStep, p);
  let lambda = 1e-3;
  let iterations = 0;

  for (let it = 0; it < 300; it++) {
    iterations = it + 1;

    // numeric Jacobian — the analytic one buys nothing at three parameters
    const JtJ = [
      [0, 0, 0],
      [0, 0, 0],
      [0, 0, 0]
    ];
    const Jtr = [0, 0, 0];
    const h = p.map((v) => Math.max(Math.abs(v) * 1e-6, 1e-9));

    for (const pt of data) {
      const f = (q: number[]) => q[0] * uStep * (1 - Math.exp(-(pt.x - q[1]) / q[2]));
      const r = f(p) - pt.y;
      const g = [0, 0, 0];
      for (let k = 0; k < 3; k++) {
        const up = [...p];
        const dn = [...p];
        up[k] += h[k];
        dn[k] -= h[k];
        g[k] = (f(up) - f(dn)) / (2 * h[k]);
      }
      for (let a = 0; a < 3; a++) {
        Jtr[a] += g[a] * r;
        for (let b = 0; b < 3; b++) JtJ[a][b] += g[a] * g[b];
      }
    }

    const A = JtJ.map((row, i) => row.map((v, j) => (i === j ? v * (1 + lambda) : v)));
    const step = solve(A, Jtr.map((v) => -v));
    if (!step) break;

    const next = [p[0] + step[0], p[1] + step[1], Math.max(p[2] + step[2], 1e-6)];
    const nextCost = fopdtCost(data, uStep, next);

    if (nextCost < cost) {
      const gain = cost - nextCost;
      p = next;
      cost = nextCost;
      lambda = Math.max(lambda / 3, 1e-12);
      if (gain < cost * 1e-12) break;
    } else {
      lambda *= 5;
      if (lambda > 1e12) break;
    }
  }

  const [K, L, T] = p;
  const pred = data.map((pt) => K * uStep * (1 - Math.exp(-(pt.x - L) / T)));
  return {
    K,
    L,
    T,
    r2: rSquared(data.map((pt) => pt.y), pred),
    n: data.length,
    iterations,
    ok: Number.isFinite(K) && Number.isFinite(L) && T > 0
  };
}

// ---------------------------------------------------------------------------
// Step 3 — the gains
// ---------------------------------------------------------------------------

export interface Gains {
  kV: number;
  kS: number;
  kA: number;
  kP: number;
  kI: number;
  kD: number;
  lambda: number;
}

/** λ-tuning: kA = (L+T)/K, kP = T/(K·λ), kI = kP/T, kD = 0. */
export function gainsFrom(steady: SteadyFit, fopdt: FopdtFit, lambdaFactor: number): Gains {
  const lambda = lambdaFactor * fopdt.T;
  const usableK = Math.abs(fopdt.K) > 1e-9 && fopdt.ok;
  const kA = usableK ? (fopdt.L + fopdt.T) / fopdt.K : 0;
  const kP = usableK && lambda > 1e-9 ? fopdt.T / (fopdt.K * lambda) : 0;
  const kI = fopdt.T > 1e-9 ? kP / fopdt.T : 0;
  return { kV: steady.kV, kS: steady.kS, kA, kP, kI, kD: 0, lambda };
}
