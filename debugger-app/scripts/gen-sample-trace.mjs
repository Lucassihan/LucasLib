// Generates a small library of synthetic LucasLib traces for development
// (no robot needed). Emits src/lib/sample/sampleAutons.ts as an array of
// { name, text } so the in-app Library panel has variety to show.
//
// Run: node scripts/gen-sample-trace.mjs
//
// Coordinates use the CENTER-origin convention (matches coords.ts default):
// field spans -72..72, heading 0 = +Y (north), clockwise positive.

import { writeFileSync, mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const TICK = 10;
const r = (n, d = 3) => Number(n.toFixed(d));
const lerp = (a, b, u) => a + (b - a) * u;
const smooth = (u) => u * u * (3 - 2 * u);
const wrap = (a) => {
  while (a > 180) a -= 360;
  while (a < -180) a += 360;
  return a;
};
const clamp = (v, m) => Math.max(-m, Math.min(m, v));
const noise = (amp) => (Math.random() * 2 - 1) * amp;
const D2R = Math.PI / 180;
const sign = (x) => (x < 0 ? -1 : 1);
// sinc with the same small-x Taylor guard the lib uses (lyPose coupling term).
const sincFn = (x) => (Math.abs(x) < 1e-3 ? 1 - (x * x) / 6 : Math.sin(x) / x);

// Unicycle (differential-drive) model: the robot can only translate along its
// own heading and rotate — no sideways motion. Lateral moves are simulated as a
// closed loop so curved approaches + coupled heading emerge naturally.
const DT = TICK / 1000; // s
const MAXV = 62; // in/s at full drive output
const MAXW = 300; // deg/s at full turn output

// Output governors (mirrors LucasLib TotalOutput + per-PID limits). Only the
// total stage is applied to the emitted motor volts; drive/turn limits are
// recorded so the debugger can show their settings/activation.
const GOV = {
  total: { min: 0, max: 12, accel: 0.6, decel: 1.2 },
  drive: { min: 0, max: 12, accel: 0, decel: 0 },
  turn: { min: 0, max: 12, accel: 0, decel: 0 }
};
function governSide(raw, last) {
  let out = clamp(raw, GOV.total.max);
  if (GOV.total.min > 0 && Math.abs(out) > 1e-4 && Math.abs(out) < GOV.total.min) {
    out = sign(out) * GOV.total.min;
  }
  const d = out - last;
  if (Math.abs(out) > Math.abs(last)) {
    if (GOV.total.accel > 0 && Math.abs(d) > GOV.total.accel) out = last + sign(d) * GOV.total.accel;
  } else {
    if (GOV.total.decel > 0 && Math.abs(d) > GOV.total.decel) out = last + sign(d) * GOV.total.decel;
  }
  return out;
}

// Build one auton trace. Returns { name, text }.
function buildAuton({ name, start, trackwidth = 11.5, headingDisableDist = 6, kLat = 0, segments }) {
  const movements = [];
  const ticks = [];
  const readWalls = [];

  let t = 0;
  const pose = { ...start };
  let mi = 0;

  // Per-movement PID internals + governor slew memory
  let pid = { mi: -1, sumLe: 0, lastLe: 0, sumHe: 0, lastHe: 0 };
  let govLast = { lo: 0, ro: 0 };

  function tick(stage, f = {}, governed = true) {
    const v = {
      le: 0, he: 0, dp: 0, hp: 0, tp: 0, sp: 0, do: 0, to: 0, lo: 0, ro: 0,
      yi: 0, xi: 0, leadX: 0, leadY: 0, leadRetraction: 0, perpDist: 0, ex: 0, ey: 0,
      carrotX: 0, carrotY: 0, ghostX: 0, ghostY: 0, arcRadius: 0, maxSlip: 0,
      ...f
    };
    if (mi !== pid.mi) {
      pid = { mi, sumLe: 0, lastLe: v.le, sumHe: 0, lastHe: v.he };
      govLast = { lo: 0, ro: 0 };
    }
    pid.sumLe = clamp(pid.sumLe + v.le * 0.01, 60);
    pid.sumHe = clamp(pid.sumHe + v.he * 0.01, 60);
    const driveI = pid.sumLe;
    const driveD = (v.le - pid.lastLe) / 0.01;
    const turnI = pid.sumHe;
    const turnD = (v.he - pid.lastHe) / 0.01;
    pid.lastLe = v.le;
    pid.lastHe = v.he;
    // govern the motor sides (cap + min floor + slew) so emitted volts are real.
    // voldrive is open-loop (voltageDrive bypasses governOutputs), so its volts
    // pass through raw — only clamped to the ±12 V motor ceiling.
    let glo, gro;
    if (governed) {
      glo = governSide(v.lo, govLast.lo);
      gro = governSide(v.ro, govLast.ro);
    } else {
      glo = clamp(v.lo, 12);
      gro = clamp(v.ro, 12);
    }
    govLast = { lo: glo, ro: gro };
    ticks.push([
      t, mi, r(pose.x), r(pose.y), r(pose.h), stage,
      r(v.le), r(v.he), r(v.dp), r(v.hp), r(v.tp), r(v.sp),
      r(v.do), r(v.to), r(glo), r(gro), r(v.yi), r(v.xi),
      r(v.leadX), r(v.leadY), r(v.leadRetraction), r(v.perpDist), r(v.ex), r(v.ey),
      r(driveI), r(driveD), r(turnI), r(turnD),
      r(v.carrotX), r(v.carrotY), r(v.ghostX), r(v.ghostY), r(v.arcRadius), r(v.maxSlip)
    ]);
  }

  function mvRow(o) {
    const d = {
      index: 0, type: '', targetX: 0, targetY: 0, targetH: 0, distance: 0,
      backwards: 0, direction: 0, partiality: 0, dLead: 0, n: 0, heading: 0,
      cosineScaling: 1, turnPriority: 0, exitTimeout: 3000,
      exitSmallError: 0, exitSmallTime: 0, exitBigError: 0, exitBigTime: 0,
      exitDist: 0, exitPerpLine: 0, exitAng: 0, exitPct: 0,
      exitReason: 'smallError', startTime: 0, endTime: 0,
      driveKp: 0, driveKi: 0, driveKd: 0, turnKp: 0, turnKi: 0, turnKd: 0,
      gLead: 0, leadChasePowerLimit: -1, pathHeading: 0, turnDisableLength: 0,
      leftVolts: 0, rightVolts: 0,
      ...o
    };
    return [
      d.index, d.type, r(d.targetX), r(d.targetY), r(d.targetH), r(d.distance),
      d.backwards, d.direction, d.partiality, d.dLead, d.n, r(d.heading),
      d.cosineScaling, d.turnPriority, d.exitTimeout, d.exitSmallError, d.exitSmallTime,
      d.exitBigError, d.exitBigTime, d.exitDist, d.exitPerpLine, d.exitAng, d.exitPct,
      d.exitReason, d.startTime, d.endTime,
      d.driveKp, d.driveKi, d.driveKd, d.turnKp, d.turnKi, d.turnKd,
      r(d.gLead), r(d.leadChasePowerLimit), r(d.pathHeading), r(d.turnDisableLength),
      r(d.leftVolts), r(d.rightVolts)
    ];
  }

  // One unicycle integration step from drive/turn outputs, then emit a tick.
  // Returns the arc radius (cached for the reactive slip cap, like the lib's
  // travelArcRadius() which reads the previous cycle).
  let lastArcRadius = 1e4;
  function lateralStep(stage, drivePow, turnPow, le, he, extra = {}) {
    const v = (drivePow / 12) * MAXV;
    const w = (turnPow / 12) * MAXW; // deg/s, + = clockwise (heading is CW-positive)
    pose.h += w * DT + noise(0.04);
    const hr = pose.h * D2R;
    pose.x += v * Math.sin(hr) * DT + noise(0.02);
    pose.y += v * Math.cos(hr) * DT + noise(0.02);
    const dsv = Math.abs(v * DT);
    const dthr = Math.abs(w * DT * D2R);
    let arcRadius = 1e4;
    if (dthr > 1e-5 && dsv > 1e-6) arcRadius = dsv / dthr;
    lastArcRadius = arcRadius;
    const doo = drivePow * Math.cos(he * D2R);
    const to = turnPow;
    tick(stage, {
      le, he, dp: drivePow, hp: turnPow, do: doo, to, lo: doo + to, ro: doo - to,
      arcRadius, ...extra
    });
    t += TICK;
  }

  // --- Movement primitives ---

  function face(targetH, opts = {}) {
    const startT = t;
    const dur = opts.dur ?? Math.max(300, Math.abs(wrap(targetH - pose.h)) * 8);
    const steps = Math.round(dur / TICK);
    const h0 = pose.h;
    for (let i = 0; i <= steps; i++) {
      const u = i / steps;
      const h = h0 + wrap(targetH - h0) * smooth(Math.min(u * 1.05, 1));
      pose.h = h + noise(0.15);
      const he = wrap(targetH - pose.h);
      const tp = clamp(he * 0.12, 12);
      tick(0, { he, tp, lo: tp, ro: -tp });
      t += TICK;
    }
    movements.push(mvRow({
      index: mi, type: 'face', targetH,
      exitSmallError: 1, exitSmallTime: 150, exitBigError: 3, exitBigTime: 400,
      exitAng: 1, startTime: startT, endTime: t,
      turnKp: 0.7, turnKd: 0.05
    }));
    mi++;
  }

  // Drive straight a fixed distance while holding a heading (unicycle).
  function driveForward(dist, opts = {}) {
    const startT = t;
    const back = opts.backwards ? true : false;
    const hHold = pose.h;
    const x0 = pose.x, y0 = pose.y;
    const signedDist = back ? -dist : dist;
    const hux = Math.sin(hHold * D2R), huy = Math.cos(hHold * D2R);
    const maxSteps = Math.round((opts.maxMs ?? 1200 + Math.abs(dist) * 45) / TICK);
    let steps = 0;
    while (steps < maxSteps) {
      const traveled = (pose.x - x0) * hux + (pose.y - y0) * huy;
      const le = signedDist - traveled;
      if (Math.abs(le) < 0.4) break;
      const he = wrap(hHold - pose.h);
      const dp = clamp(le * 0.45, 12);
      const tp = clamp(he * 0.8, 12);
      lateralStep(0, dp, tp, le, he, { yi: traveled });
      steps++;
    }
    movements.push(mvRow({
      index: mi, type: back ? 'driveBackward' : 'driveForward',
      distance: dist, heading: hHold, backwards: back ? 1 : 0,
      exitSmallError: 1, exitSmallTime: 100, exitBigError: 3, exitBigTime: 500,
      startTime: startT, endTime: t,
      driveKp: 0.45, driveKi: 0.01, driveKd: 0.1, turnKp: 0.8, turnKd: 0.05
    }));
    mi++;
  }

  // Move to a point (unicycle): steer the heading toward the target while
  // driving (cosine-scaled, so a bad initial heading curves in instead of
  // strafing). Heading correction disables within headingDisableDist — it just
  // drives the last bit straight, so the path curves like a real PTP.
  function moveTo(targetX, targetY, opts = {}) {
    const startT = t;
    const exitDist = opts.exitDist ?? 3;
    const hdd = opts.hdd ?? headingDisableDist;
    const maxSteps = Math.round((opts.maxMs ?? 4000) / TICK);
    let close = false, steps = 0;
    while (steps < maxSteps) {
      const dx = targetX - pose.x, dy = targetY - pose.y;
      const dist = Math.hypot(dx, dy);
      if (dist < exitDist) break;
      const pointHeading = (Math.atan2(dx, dy) * 180) / Math.PI;
      let he = wrap(pointHeading - pose.h);
      if (dist < hdd) close = true;
      let stage = 0;
      if (close) {
        stage = 1;
        he = 0; // heading disabled near the target — drive straight in
      }
      const dp = clamp(dist * 0.45, 12) * Math.max(0, Math.cos(he * D2R));
      const tp = clamp(he * 0.9, 12);
      lateralStep(stage, dp, tp, dist, he);
      steps++;
    }
    movements.push(mvRow({
      index: mi, type: 'moveTo', targetX, targetY,
      exitDist, exitReason: 'exitDist',
      exitSmallError: 1, exitSmallTime: 100, exitBigError: 3, exitBigTime: 500,
      startTime: startT, endTime: t,
      driveKp: 0.45, driveKi: 0.01, driveKd: 0.1, turnKp: 0.8, turnKd: 0.05
    }));
    mi++;
  }

  // Three-stage ghost-point boomerang, matching LucasLib boomerangTo:
  //   carrot = end + dir·s·initialLeadDist, s = monotonic-min(robot→carrot)/init
  //   ghost  = initCarrot + (carrot − initCarrot)·(1 − gLead)
  //   stage: distCarrot<0.7 → END(2); distGhost<ghostSwitch → MID(1); else FAR(0)
  // The robot pose is a synthetic Bézier approach, but every lead-point and
  // distance is computed with the real formulas so the overlay is faithful.
  function boomerangTo(targetX, targetY, targetH, opts = {}) {
    const startT = t;
    const P0 = { x: pose.x, y: pose.y };
    const lead = opts.dLead ?? 0.5;
    const gLead = opts.gLead ?? 0.3;
    const chaseLimit = opts.leadChasePowerLimit ?? -1;

    const initialDist = Math.hypot(targetX - P0.x, targetY - P0.y);
    const initialLeadDist = lead * initialDist;
    // forward: leadSign −1, so the carrot sits behind the target on the approach side
    const dirX = -Math.sin(targetH * D2R);
    const dirY = -Math.cos(targetH * D2R);
    const initCarrotX = targetX + dirX * initialLeadDist;
    const initCarrotY = targetY + dirY * initialLeadDist;
    // unit vector ALONG the target heading (for the perpendicular cross-track)
    const hX = Math.sin(targetH * D2R);
    const hY = Math.cos(targetH * D2R);

    let carrotX = initCarrotX;
    let carrotY = initCarrotY;
    const initCarrotDist = Math.hypot(initCarrotX - P0.x, initCarrotY - P0.y);
    let minCarrotDist = initCarrotDist;
    const ghostSwitchDist = 2.0; // 2·(maxOutput/12)
    let closeGhost = false;
    let closeEnd = false;
    const maxSteps = Math.round((opts.maxMs ?? 5000) / TICK);
    let steps = 0;

    while (steps < maxSteps) {
      // retract the carrot (monotonic-min of robot→carrot distance)
      const curCarrotDist = Math.hypot(carrotX - pose.x, carrotY - pose.y);
      if (curCarrotDist < minCarrotDist) minCarrotDist = curCarrotDist;
      const s = initCarrotDist > 0 ? minCarrotDist / initCarrotDist : 0;
      carrotX = targetX + dirX * s * initialLeadDist;
      carrotY = targetY + dirY * s * initialLeadDist;
      // ghost trails the carrot toward the original carrot by gLead
      const ghostX = initCarrotX + (carrotX - initCarrotX) * (1 - gLead);
      const ghostY = initCarrotY + (carrotY - initCarrotY) * (1 - gLead);

      const distEnd = Math.hypot(targetX - pose.x, targetY - pose.y);
      const distCarrot = Math.hypot(carrotX - pose.x, carrotY - pose.y);
      const distGhost = Math.hypot(ghostX - pose.x, ghostY - pose.y);
      if (distEnd < 1.0) break; // settled at the pose

      // staging (one-way latches) + the active steer point
      let stage = 0;
      let aimX = ghostX;
      let aimY = ghostY;
      if (distCarrot < 0.7 || closeEnd) {
        closeEnd = true;
        stage = 2;
        aimX = targetX;
        aimY = targetY;
      } else if (distGhost < ghostSwitchDist || closeGhost) {
        closeGhost = true;
        stage = 1;
        aimX = carrotX;
        aimY = carrotY;
      }
      const aimHeading = (Math.atan2(aimX - pose.x, aimY - pose.y) * 180) / Math.PI;
      let he = wrap(aimHeading - pose.h);
      if (stage === 2 && distEnd < 0.7) he = wrap(targetH - pose.h);

      // cross-track: perpendicular distance from robot to the θ-line through end
      const perp = (pose.x - targetX) * hY - (pose.y - targetY) * hX;

      // centripetal slip cap uses the PREVIOUS cycle's measured arc radius
      let maxSlip = 0;
      if (chaseLimit > 0) maxSlip = Math.sqrt(chaseLimit * Math.abs(lastArcRadius));

      // SPEED tracks the true target distance, cosine-scaled, then slip-capped
      let dp = clamp(distEnd * 0.45, 12) * Math.max(0, Math.cos(he * D2R));
      if (chaseLimit > 0 && !(stage === 2 && distEnd < 0.7) && Math.abs(dp) > maxSlip) {
        dp = sign(dp) * maxSlip;
      }
      const tp = clamp(he * 0.8, 12);
      lateralStep(stage, dp, tp, distEnd, he, {
        leadX: carrotX, leadY: carrotY, leadRetraction: s, perpDist: Math.abs(perp),
        carrotX, carrotY, ghostX, ghostY, maxSlip
      });
      steps++;
    }
    movements.push(mvRow({
      index: mi, type: 'boomerangTo', targetX, targetY, targetH,
      dLead: lead, gLead, leadChasePowerLimit: chaseLimit, turnPriority: 0.4,
      exitSmallError: 0.5, exitSmallTime: 200, exitBigError: 3, exitBigTime: 500,
      exitPerpLine: 10, exitReason: 'perpLine',
      startTime: startT, endTime: t,
      driveKp: 0.45, driveKi: 0.01, driveKd: 0.1, turnKp: 0.8, turnKd: 0.05
    }));
    mi++;
  }

  // lyPose: path-follow Lyapunov drive to a pose. Commits to a FIXED line
  // through (x, y) at pathHeading (explicit, or bot->target frozen at start) and
  // rides it in. Control law mirrors LucasLib lyapunovCalculate:
  //   e_x = signed along-line distance, e_y = signed cross-track offset,
  //   e_theta = robotH - pathHeading; v = |cos eθ|·v_d; ω = -(PID(eθ) + kLat·v·e_y·sinc(eθ))
  function lyPose(targetX, targetY, opts = {}) {
    const startT = t;
    let pathHeading = opts.heading;
    if (pathHeading === undefined || pathHeading === null || Number.isNaN(pathHeading)) {
      pathHeading = (Math.atan2(targetX - pose.x, targetY - pose.y) * 180) / Math.PI;
    }
    const refH = pathHeading * D2R;
    const exitDist = opts.exitDist ?? 1.5;
    const turnDisableLength = opts.turnDisableLength ?? 3;
    const maxSteps = Math.round((opts.maxMs ?? 5000) / TICK);
    let steps = 0;
    while (steps < maxSteps) {
      const dx = targetX - pose.x;
      const dy = targetY - pose.y;
      const dist = Math.hypot(dx, dy);
      if (dist < exitDist) break;
      // rotate the error into the fixed path frame
      const e_x = Math.sin(refH) * dx + Math.cos(refH) * dy; // along-line dist to target
      const e_y = -Math.cos(refH) * dx + Math.sin(refH) * dy; // signed cross-track
      const e_theta = wrap(pose.h - pathHeading); // deg
      const etr = e_theta * D2R;
      const v_d = clamp(e_x * 0.45, 12); // drivePID on e_x
      const v = Math.abs(Math.cos(etr)) * v_d; // cosine-scaled forward
      const pidRaw = clamp(e_theta * 0.8, 12); // headingPID(eθ)
      const latRaw = kLat * v * e_y * sincFn(etr); // Lyapunov coupling
      let omega = -(pidRaw + latRaw);
      omega = clamp(omega, 12);
      // CLOSE stage: within turnDisableLength, kill the turn and settle straight in
      let stage = 0;
      if (dist < turnDisableLength) {
        stage = 1;
        omega = 0;
      }
      // unicycle integrate (v = forward, omega = turn)
      const vel = (v / 12) * MAXV;
      const w = (omega / 12) * MAXW;
      pose.h += w * DT + noise(0.04);
      const hr = pose.h * D2R;
      pose.x += vel * Math.sin(hr) * DT + noise(0.02);
      pose.y += vel * Math.cos(hr) * DT + noise(0.02);
      const dsv = Math.abs(vel * DT);
      const dthr = Math.abs(w * DT * D2R);
      let arcRadius = 1e4;
      if (dthr > 1e-5 && dsv > 1e-6) arcRadius = dsv / dthr;
      lastArcRadius = arcRadius;
      tick(stage, {
        le: dist, he: e_theta, dp: v_d, hp: omega, do: v, to: omega,
        lo: v + omega, ro: v - omega, ex: e_x, ey: e_y, arcRadius
      });
      t += TICK;
      steps++;
    }
    movements.push(mvRow({
      index: mi, type: 'lyPose', targetX, targetY, targetH: pathHeading,
      heading: pathHeading, pathHeading, turnDisableLength,
      exitDist, exitReason: 'exitDist',
      exitSmallError: 0.5, exitSmallTime: 150, exitBigError: 3, exitBigTime: 500,
      startTime: startT, endTime: t,
      driveKp: 0.45, driveKi: 0, driveKd: 0.05, turnKp: 0.8, turnKi: 0, turnKd: 0.05
    }));
    mi++;
  }

  // voldrive: open-loop voltage. Unlike every other movement it has NO PID, NO
  // exit condition, and is NOT its own loop — voldrive(left,right) just commands
  // the motors and returns; the caller's delay() decides how long it runs. The
  // sim integrates the unicycle at the fixed left/right volts for that duration.
  function voldrive(left, right, durationMs) {
    const startT = t;
    const driveOut = (left + right) / 2;
    const turnOut = (left - right) / 2;
    const steps = Math.round(durationMs / TICK);
    for (let i = 0; i < steps; i++) {
      const v = (driveOut / 12) * MAXV;
      const w = (turnOut / 12) * MAXW; // deg/s, + = clockwise
      pose.h += w * DT + noise(0.04);
      const hr = pose.h * D2R;
      pose.x += v * Math.sin(hr) * DT + noise(0.02);
      pose.y += v * Math.cos(hr) * DT + noise(0.02);
      const dsv = Math.abs(v * DT);
      const dthr = Math.abs(w * DT * D2R);
      let arcRadius = 1e4;
      if (dthr > 1e-5 && dsv > 1e-6) arcRadius = dsv / dthr;
      lastArcRadius = arcRadius;
      // open loop: no PID terms; volts pass through ungoverned (governed=false)
      tick(0, { do: driveOut, to: turnOut, lo: left, ro: right, arcRadius }, false);
      t += TICK;
    }
    movements.push(mvRow({
      index: mi, type: 'voldrive', leftVolts: left, rightVolts: right,
      exitReason: 'duration', exitTimeout: durationMs,
      startTime: startT, endTime: t
    }));
    mi++;
  }

  // Swing: lock one wheel, rotate about it. The chassis center traces an arc
  // of radius trackwidth/2 about the locked wheel.
  function swing(side /* 'left'|'right' */, targetH, opts = {}) {
    const startT = t;
    const h0 = pose.h;
    const sweep = wrap(targetH - h0);
    const dur = opts.dur ?? Math.max(400, Math.abs(sweep) * 10);
    const steps = Math.round(dur / TICK);
    // pivot = locked wheel position
    const hr0 = h0 * D2R;
    const lockX = side === 'left' ? -Math.cos(hr0) : Math.cos(hr0);
    const lockY = side === 'left' ?  Math.sin(hr0) : -Math.sin(hr0);
    const pivotX = pose.x + lockX * (trackwidth / 2);
    const pivotY = pose.y + lockY * (trackwidth / 2);

    for (let i = 0; i <= steps; i++) {
      const u = i / steps;
      const h = h0 + sweep * smooth(Math.min(u * 1.05, 1));
      pose.h = h + noise(0.18);
      // center position = pivot + perpendicular (toward the moving side) * trackwidth/2
      const hr = h * D2R;
      const offX = side === 'left' ?  Math.cos(hr) : -Math.cos(hr);
      const offY = side === 'left' ? -Math.sin(hr) :  Math.sin(hr);
      pose.x = pivotX + offX * (trackwidth / 2) + noise(0.08);
      pose.y = pivotY + offY * (trackwidth / 2) + noise(0.08);
      const he = wrap(targetH - pose.h);
      const sp = clamp(he * 0.12, 12);
      // swing: one side drives, the other holds
      const movingPow = sp;
      const lo = side === 'left' ? 0 : movingPow;
      const ro = side === 'left' ? movingPow : 0;
      tick(0, { he, sp, lo, ro });
      t += TICK;
    }

    const typeName = side === 'left' ? 'swingOnLeft' : 'swingOnRight';
    movements.push(mvRow({
      index: mi, type: typeName, targetH,
      exitSmallError: 1, exitSmallTime: 150, exitBigError: 3, exitBigTime: 400,
      exitAng: 1, startTime: startT, endTime: t,
      turnKp: 0.5, turnKd: 0.04
    }));
    mi++;
  }

  // Run segments
  for (const seg of segments) seg({ face, driveForward, moveTo, boomerangTo, swing, lyPose, voldrive });

  // Assemble trace text
  const COLS_M =
    'index,type,targetX,targetY,targetH,distance,backwards,direction,partiality,dLead,n,heading,cosineScaling,turnPriority,exitTimeout,exitSmallError,exitSmallTime,exitBigError,exitBigTime,exitDist,exitPerpLine,exitAng,exitPct,exitReason,startTime,endTime,driveKp,driveKi,driveKd,turnKp,turnKi,turnKd,gLead,leadChasePowerLimit,pathHeading,turnDisableLength,leftVolts,rightVolts';
  const COLS_T =
    't,mi,x,y,h,stage,le,he,dp,hp,tp,sp,do,to,lo,ro,yi,xi,leadX,leadY,leadRetraction,perpDist,ex,ey,driveI,driveD,turnI,turnD,carrotX,carrotY,ghostX,ghostY,arcRadius,maxSlip';
  const COLS_R = 't,mi,wall,sensor,preX,preY,preH,postX,postY,postH';

  let out = '';
  out += '#LUCASLIB_TRACE v1\n';
  out += `#auton_name=${name}\n`;
  out += `#start_pose=${r(start.x)},${r(start.y)},${r(start.h)}\n`;
  out += `#trackwidth=${trackwidth}\n`;
  out += `#headingDisableDist=${headingDisableDist}\n`;
  out += `#useLyapunov=${kLat > 0 ? 1 : 0}\n`;
  out += `#kLat=${kLat}\n`;
  out += `#gov_total_min=${GOV.total.min}\n#gov_total_max=${GOV.total.max}\n#gov_total_accel=${GOV.total.accel}\n#gov_total_decel=${GOV.total.decel}\n`;
  out += `#gov_drive_min=${GOV.drive.min}\n#gov_drive_max=${GOV.drive.max}\n#gov_drive_accel=${GOV.drive.accel}\n#gov_drive_decel=${GOV.drive.decel}\n`;
  out += `#gov_turn_min=${GOV.turn.min}\n#gov_turn_max=${GOV.turn.max}\n#gov_turn_accel=${GOV.turn.accel}\n#gov_turn_decel=${GOV.turn.decel}\n\n`;
  out += '[MOVEMENTS]\n' + COLS_M + '\n' + movements.map((m) => m.join(',')).join('\n') + '\n\n';
  out += '[TICKS]\n' + COLS_T + '\n' + ticks.map((row) => row.join(',')).join('\n') + '\n\n';
  out += '[READWALLS]\n' + COLS_R + '\n' + readWalls.map((w) => w.join(',')).join('\n') + '\n';

  return { name, text: out, mvCount: movements.length, tickCount: ticks.length };
}

// --- Define 4 sample autons covering varied movement combos ---

const autons = [];

// 1. The original demo: face + drive + boomerang + moveTo
autons.push(buildAuton({
  name: 'sample_demo',
  start: { x: -48, y: -48, h: 0 },
  segments: [
    ({ face }) => face(90),
    ({ driveForward }) => driveForward(72),
    ({ boomerangTo }) => boomerangTo(48, 36, 0, { gLead: 0.35, leadChasePowerLimit: 2 }),
    ({ moveTo }) => moveTo(0, 12),
    // open-loop curve: voldrive(6,3) for 900 ms — left faster than right, so it
    // arcs clockwise with no PID/exit, running purely on the caller's delay
    ({ voldrive }) => voldrive(6, 3, 900)
  ]
}));

// 2. Skills route: lots of moveTo + facePoint-like spot turns + driveForward
autons.push(buildAuton({
  name: 'skills_route',
  start: { x: -60, y: -60, h: 0 },
  kLat: 0.25,
  segments: [
    ({ driveForward }) => driveForward(48),
    ({ face }) => face(90),
    ({ driveForward }) => driveForward(36),
    // align north first, then an implicit-heading lyPose rides the line in like a
    // steady, accurate straightline (its signature use case)
    ({ face }) => face(0),
    ({ lyPose }) => lyPose(-24, 24),
    ({ boomerangTo }) => boomerangTo(36, 48, 0)
  ]
}));

// 3. Swing showcase: spot turn + swings on both sides + driveBackward
autons.push(buildAuton({
  name: 'swing_showcase',
  start: { x: -36, y: -24, h: 0 },
  segments: [
    ({ face }) => face(45),
    ({ swing }) => swing('left', 135),
    ({ driveForward }) => driveForward(24),
    ({ swing }) => swing('right', 45),
    ({ driveForward }) => driveForward(20, { backwards: true }),
    // open-loop ram: voldrive(8,8) for 700 ms — drives straight at fixed volts
    ({ voldrive }) => voldrive(8, 8, 700)
  ]
}));

// 4. Match auton: full variety, longer run
autons.push(buildAuton({
  name: 'match_left_red',
  start: { x: -60, y: -36, h: 0 },
  kLat: 0.15,
  segments: [
    ({ driveForward }) => driveForward(40),
    ({ face }) => face(-90),
    ({ driveForward }) => driveForward(30, { backwards: true }),
    ({ swing }) => swing('right', 60),
    // implicit-heading lyPose: robot is roughly on the bot->target line, rides it in
    ({ lyPose }) => lyPose(24, -12),
    ({ boomerangTo }) => boomerangTo(48, -48, 90),
    ({ face }) => face(180)
  ]
}));

// 5. lyPose showcase: path-follow Lyapunov. Starts off the line so the
// cross-track recovery is visible; one implicit-heading + one explicit-heading.
autons.push(buildAuton({
  name: 'lypose_demo',
  start: { x: 8, y: -30, h: 20 },
  // sim-tuned coupling (the unicycle's volt->speed scaling needs a higher kLat
  // than the robot's real 0.04 to merge fully before the 3in turn-disable zone)
  kLat: 0.25,
  segments: [
    // explicit path heading 0 (vertical line x=24): robot starts ~16in west of
    // the line, merges onto it, then rides north to the pose
    ({ lyPose }) => lyPose(24, 36, { heading: 0 }),
    // orient toward the next target, then implicit-heading lyPose (the bot->target
    // line frozen at the start) — straightline-style with a steady final heading
    ({ face }) => face(238),
    ({ lyPose }) => lyPose(-12, 12),
    // face east, then explicit heading-90 line (horizontal y=2): another off-line
    // start so the cross-track merge is visible the other direction
    ({ face }) => face(90),
    ({ lyPose }) => lyPose(36, 2, { heading: 90 })
  ]
}));

// 6. Override Match tour: a grand tour designed for the "Override Match"
// backdrop. The robot collects cups (the yellow pieces on the tile grid) and
// navigates AROUND the field's 3D structures — never through them:
//   • cross/stake clusters sit on the TL–BR diagonal at (-48,47) (-24,23)
//     (24,-23) (48,-47);
//   • square goals: blue (25,48) (48,24), red (-23,-48) (-47,-25);
//   • corner goals at (±62, ±55), edge goals on the walls.
// The BL–TR diagonal (y = x) threads clean between all of them, so the central
// pickups ride straight up it. lyPose is used twice to drive UP TO a goal and
// stop just short (the robot can't overlap the raised goal); boomerang aligns
// the final score into the blue corner goal.
autons.push(buildAuton({
  name: 'override_match',
  start: { x: -48, y: -60, h: 0 },
  kLat: 0.25,
  segments: [
    // grab the bottom-left cup, then swing onto the clear y = x diagonal
    ({ moveTo }) => moveTo(-48, -48),
    ({ swing }) => swing('right', 30),
    ({ moveTo }) => moveTo(-24, -24),
    // turn around and lyPose DOWN to the red square goal, stopping ~15in short
    // (the robot can't overlap the raised goal — it just pulls up to it)
    ({ face }) => face(180),
    ({ lyPose }) => lyPose(-23, -33, { heading: 180 }),
    ({ driveForward }) => driveForward(10, { backwards: true }),
    // ride the obstacle-free diagonal up through center, collecting cups
    ({ moveTo }) => moveTo(24, 24),
    ({ moveTo }) => moveTo(48, 48),
    // lyPose down to the blue square goal, again pulling up just short of it
    ({ face }) => face(180),
    ({ lyPose }) => lyPose(48, 39, { heading: 180 }),
    ({ driveForward }) => driveForward(10, { backwards: true }),
    // boomerang in to align the score at the blue corner goal (stops ~16in out),
    // then a short open-loop nudge — still clear of the goal itself
    ({ boomerangTo }) => boomerangTo(50, 43, 40),
    ({ voldrive }) => voldrive(5, 5, 120)
  ]
}));

const here = dirname(fileURLToPath(import.meta.url));
const outDir = join(here, '..', 'src', 'lib', 'sample');
mkdirSync(outDir, { recursive: true });
const outFile = join(outDir, 'sampleAutons.ts');
writeFileSync(
  outFile,
  '// AUTO-GENERATED by scripts/gen-sample-trace.mjs — do not edit by hand.\n' +
    'export interface SampleAuton { name: string; text: string }\n' +
    `export const sampleAutons: SampleAuton[] = ${JSON.stringify(
      autons.map(({ name, text }) => ({ name, text })),
      null,
      0
    )};\n`
);

// also keep the single-trace export for backward-compat with anything that
// imports sampleTrace directly
const legacyFile = join(outDir, 'sampleTrace.ts');
writeFileSync(
  legacyFile,
  '// AUTO-GENERATED by scripts/gen-sample-trace.mjs — do not edit by hand.\n' +
    'import { sampleAutons } from "./sampleAutons";\n' +
    'export const sampleTrace = sampleAutons[0].text;\n'
);

console.log(
  `wrote ${outFile}: ${autons.length} autons`,
  autons.map((a) => `  - ${a.name}: ${a.mvCount} movements, ${a.tickCount} ticks`).join('\n')
);
