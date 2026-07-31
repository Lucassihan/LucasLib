// Kinematic port of LucasLib's three-stage ghost-point boomerang (boomerangTo),
// reduced to a carrot-pursuit sim so a STATIC diagram can show the same curve,
// lead axis and carrot/ghost anchors the controller produces.
//
// Field convention (matches LucasLib odometry): heading 0 = +Y (up), CW positive,
// forward vector = (sin h, cos h), pointHeading = atan2(dx, dy).

export interface Pose {
  x: number;
  y: number;
  heading: number; // degrees
}

export interface BoomSample {
  x: number;
  y: number;
  heading: number; // robot facing (degrees)
  carrot: { x: number; y: number };
  ghost: { x: number; y: number };
  s: number; // retraction factor (1 → 0)
}

export interface BoomResult {
  samples: BoomSample[];
  initCarrot: { x: number; y: number };
  target: { x: number; y: number };
  cumLen: number[];
  totalLen: number;
}

const D2R = Math.PI / 180;
const R2D = 180 / Math.PI;

function wrap(a: number): number {
  while (a > 180) a -= 360;
  while (a < -180) a += 360;
  return a;
}

export interface BoomOpts {
  lead: number;
  gLead: number;
  backward: boolean;
  stepLen?: number;
  maxTurnDeg?: number;
}

export function simulateBoomerang(start: Pose, target: Pose, opts: BoomOpts): BoomResult {
  const stepLen = opts.stepLen ?? 0.6;
  const maxTurn = opts.maxTurnDeg ?? 5.5;
  const lead = Math.max(0, opts.lead);
  const gLead = Math.min(1, Math.max(0, opts.gLead));

  const d0 = Math.hypot(target.x - start.x, target.y - start.y) || 1e-6;

  // Carrot axis: planted `lead·d0` off the target along its facing. leadSign −1
  // for forward (robot approaches from behind the target's facing), +1 backward.
  const leadSign = opts.backward ? 1 : -1;
  const fT = { x: Math.sin(target.heading * D2R), y: Math.cos(target.heading * D2R) };
  const dir = { x: leadSign * fT.x, y: leadSign * fT.y };
  const initialLeadDist = lead * d0;
  const initCarrot = {
    x: target.x + dir.x * initialLeadDist,
    y: target.y + dir.y * initialLeadDist
  };

  // Stage-switch radii, straight from boomerangTo: ghost→carrot at
  // 6·(maxOutput/12 V) — 6 in at the full 12 V scale a diagram assumes — and
  // carrot→target once the (by then fully retracted) carrot is within 0.7 in.
  const ghostSwitch = 6;
  const carrotSwitch = 0.7;
  // Distance inside which drivePID (kP 0.5) stops saturating its 12 V ceiling.
  const slowRadius = 24;

  // Retraction is driven by the robot→CARROT distance, not the robot→target
  // distance: closing on the carrot is what pulls the carrot (and with it the
  // ghost) down onto the target. Normalising by the target distance instead
  // lets `s` stall whenever the robot is not closing on the target itself.
  const initCarrotDist = Math.hypot(initCarrot.x - start.x, initCarrot.y - start.y) || 1e-6;
  let minCarrotDist = initCarrotDist;
  let carrot = { x: initCarrot.x, y: initCarrot.y };

  // Sticky stage latches: FAR → MID → END, one-way (never fall back to an aim
  // point the robot has already passed).
  let closeGhost = false;
  let closeEnd = false;

  const samples: BoomSample[] = [];
  let px = start.x;
  let py = start.y;
  let travelH = start.heading; // heading of MOTION
  let minEnd = Infinity;

  const cap = Math.ceil(((d0 + initialLeadDist) / stepLen) * 12) + 400;
  for (let i = 0; i < cap; i++) {
    // --- Retract the carrot (monotonic-min of the robot→carrot distance) ---
    const curCarrotDist = Math.hypot(carrot.x - px, carrot.y - py);
    if (curCarrotDist < minCarrotDist) minCarrotDist = curCarrotDist;
    const s = minCarrotDist / initCarrotDist;
    carrot = { x: target.x + dir.x * s * initialLeadDist, y: target.y + dir.y * s * initialLeadDist };

    // --- Ghost trails behind the carrot toward the original carrot by gLead ---
    const ghost = {
      x: initCarrot.x + (carrot.x - initCarrot.x) * (1 - gLead),
      y: initCarrot.y + (carrot.y - initCarrot.y) * (1 - gLead)
    };

    const distEnd = Math.hypot(target.x - px, target.y - py);
    const distCarrot = Math.hypot(carrot.x - px, carrot.y - py);
    const distGhost = Math.hypot(ghost.x - px, ghost.y - py);

    // Three-stage aim: FAR→ghost, MID→carrot, END→target.
    let aim: { x: number; y: number };
    if (distCarrot < carrotSwitch || closeEnd) {
      closeEnd = true;
      aim = target;
    } else if (distGhost < ghostSwitch || closeGhost) {
      closeGhost = true;
      aim = carrot;
    } else {
      aim = ghost;
    }

    const facing = opts.backward ? travelH + 180 : travelH;
    samples.push({ x: px, y: py, heading: facing, carrot: { ...carrot }, ghost, s });

    // Drive distance drives SPEED, exactly as boomerangTo picks its lateralError.
    const driveDist = closeEnd || distEnd > distCarrot ? distEnd : distCarrot;

    // A tick's travel shrinks with drive power, so the reachable turn radius
    // (travel / maxTurn) tightens as the robot slows — which is the only reason
    // a real chassis can capture a target the full-speed radius would orbit.
    // drivePID is kP 0.5 against a 12 V ceiling, so power ramps down linearly
    // inside 24 in; the floor keeps the step count finite.
    const step = stepLen * Math.min(1, Math.max(0.05, driveDist / slowRadius));

    // Pursuit ends at the closest approach: a step from the target, or the tick
    // the robot starts receding inside the 0.7 in where boomerangTo stops
    // steering at the point and hands off to its settle brake. Ending on the
    // approach (rather than on a fixed radius) keeps the tail tangent — cutting
    // out at a fixed 0.7 in leaves the robot crossing, and the arrival sample
    // then hooks sideways to reach the target.
    if (distEnd <= step) break;
    if (distEnd < carrotSwitch && distEnd > minEnd) break;
    minEnd = Math.min(minEnd, distEnd);

    // Steer motion toward the aim point, rate-limited for a smooth arc.
    const desired = Math.atan2(aim.x - px, aim.y - py) * R2D;
    travelH += Math.max(-maxTurn, Math.min(maxTurn, wrap(desired - travelH)));
    px += Math.sin(travelH * D2R) * step;
    py += Math.cos(travelH * D2R) * step;
  }

  // Clean arrival exactly on target at the arrival facing — but only when the
  // remaining gap is too small to draw as a chord. When the turn-rate cap makes
  // a tight capture impossible the robot really does pass beside the target,
  // and bridging that gap would draw a hook the controller never produces.
  const last = samples[samples.length - 1];
  if (Math.hypot(target.x - last.x, target.y - last.y) <= 0.15) {
    samples.push({
      x: target.x,
      y: target.y,
      heading: target.heading,
      carrot: { x: target.x, y: target.y },
      ghost: { x: target.x, y: target.y },
      s: 0
    });
  }

  const cumLen: number[] = [0];
  let total = 0;
  for (let i = 1; i < samples.length; i++) {
    total += Math.hypot(samples[i].x - samples[i - 1].x, samples[i].y - samples[i - 1].y);
    cumLen.push(total);
  }

  return { samples, initCarrot, target: { x: target.x, y: target.y }, cumLen, totalLen: total };
}

/** Interpolate a pose (x, y, facing) at arc-length fraction t∈[0,1] of a result. */
export function boomPoseAt(res: BoomResult, t: number): Pose & { carrot: { x: number; y: number }; ghost: { x: number; y: number } } {
  const target = t * res.totalLen;
  const s = res.samples;
  if (s.length === 1) return { ...s[0] };
  let i = 1;
  while (i < s.length && res.cumLen[i] < target) i++;
  const a = s[i - 1];
  const b = s[Math.min(i, s.length - 1)];
  const seg = res.cumLen[Math.min(i, s.length - 1)] - res.cumLen[i - 1] || 1;
  const f = Math.min(1, Math.max(0, (target - res.cumLen[i - 1]) / seg));
  const lerp = (u: number, v: number) => u + (v - u) * f;
  const ha = a.heading;
  const hb = a.heading + wrap(b.heading - a.heading);
  return {
    x: lerp(a.x, b.x),
    y: lerp(a.y, b.y),
    heading: lerp(ha, hb),
    carrot: { x: lerp(a.carrot.x, b.carrot.x), y: lerp(a.carrot.y, b.carrot.y) },
    ghost: { x: lerp(a.ghost.x, b.ghost.x), y: lerp(a.ghost.y, b.ghost.y) }
  };
}
