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
  const initCarrot = { x: target.x + dir.x * lead * d0, y: target.y + dir.y * lead * d0 };

  const ghostSwitch = Math.max(6, 0.14 * d0);
  const endThresh = 2.5;

  const samples: BoomSample[] = [];
  let px = start.x;
  let py = start.y;
  let travelH = start.heading; // heading of MOTION
  let prevS = 1;
  let prevDist = Infinity;

  const cap = Math.ceil((d0 / stepLen) * 4) + 40;
  for (let i = 0; i < cap; i++) {
    const distEnd = Math.hypot(target.x - px, target.y - py);
    const s = Math.min(prevS, distEnd / d0);
    prevS = s;

    const carrot = { x: target.x + dir.x * s * lead * d0, y: target.y + dir.y * s * lead * d0 };
    const ghost = {
      x: initCarrot.x + (carrot.x - initCarrot.x) * (1 - gLead),
      y: initCarrot.y + (carrot.y - initCarrot.y) * (1 - gLead)
    };

    // Three-stage aim: FAR→ghost, MID→carrot, NEAR→target.
    let aim = ghost;
    const distGhost = Math.hypot(ghost.x - px, ghost.y - py);
    if (distEnd < endThresh) aim = target;
    else if (distGhost < ghostSwitch) aim = carrot;

    const facing = opts.backward ? travelH + 180 : travelH;
    samples.push({ x: px, y: py, heading: facing, carrot, ghost, s });

    // Converged, or past the closest approach (distance now growing) in the
    // second half of the move — stop here so the sim never orbits the target
    // when the turn-rate cap makes a tight capture impossible.
    if (distEnd < 1.2) break;
    if (distEnd < 0.5 * d0 && distEnd > prevDist + 0.05) break;
    prevDist = distEnd;

    // Steer motion toward the aim point, rate-limited for a smooth arc.
    const desired = Math.atan2(aim.x - px, aim.y - py) * R2D;
    travelH += Math.max(-maxTurn, Math.min(maxTurn, wrap(desired - travelH)));
    px += Math.sin(travelH * D2R) * stepLen;
    py += Math.cos(travelH * D2R) * stepLen;
  }

  // Clean arrival exactly on target at the arrival facing.
  samples.push({
    x: target.x,
    y: target.y,
    heading: target.heading,
    carrot: { x: target.x, y: target.y },
    ghost: { x: target.x, y: target.y },
    s: 0
  });

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
