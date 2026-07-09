import type {
  BoomParams,
  Element,
  PathEl,
  PathKind,
  Pt,
  RobotChassis,
  RobotEl,
  ShapeEl,
  StampEl,
  StampKind,
  TextEl
} from './types';

// Real top-down footprint of the Push Back goal (inches), measured from the
// 4000px field render (≈27.04 px/in): the base+tabs span ≈10" across. The stamp
// art fills its 512px canvas edge-to-edge, so this maps 1:1 to the placed size.
export const GOAL_FOOTPRINT_IN = 10;

export const INK = '#1f2937'; // default dark "ink" so elements read on white paper
export const LINE = '#2563eb'; // default path blue

let seq = 0;
export function uid(): string {
  return typeof crypto !== 'undefined' && crypto.randomUUID
    ? crypto.randomUUID()
    : `el_${Date.now()}_${seq++}`;
}

export function defaultChassis(): RobotChassis {
  return {
    widthHoles: 30,
    lengthHoles: 30,
    driveWheelCount: 6,
    sixWheelLayout: 'centered',
    intakePosition: 'front',
    trackingCenterIndex: 2
  };
}

export function createRobot(x: number, y: number, heading = 0): RobotEl {
  return {
    id: uid(),
    type: 'robot',
    x,
    y,
    heading,
    chassis: defaultChassis(),
    color: INK,
    fill: true,
    opacity: 1
  };
}

function headingOf(a: Pt, b: Pt): number {
  return (Math.atan2(b.x - a.x, b.y - a.y) * 180) / Math.PI;
}

export function defaultBoom(startHeading: number, targetHeading: number): BoomParams {
  return {
    lead: 0.5,
    gLead: 0.5,
    startHeading,
    targetHeading,
    backward: false,
    showLeads: true,
    robotT: null,
    chassis: defaultChassis()
  };
}

export function createPath(kind: PathKind, a: Pt, b: Pt): PathEl {
  const base = {
    id: uid(),
    type: 'path' as const,
    color: LINE,
    width: 1.4,
    dash: 'solid' as const,
    arrows: 'end' as const,
    opacity: 1
  };
  const mid = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
  const dir = headingOf(a, b);
  // perpendicular offset for a nice default arc bow
  const len = Math.hypot(b.x - a.x, b.y - a.y) || 1;
  const perp = { x: -(b.y - a.y) / len, y: (b.x - a.x) / len };
  const bow = len * 0.25;

  switch (kind) {
    case 'straight':
      return { ...base, kind, points: [a, b], arrows: 'end' };
    case 'arc':
      return { ...base, kind, points: [a, { x: mid.x + perp.x * bow, y: mid.y + perp.y * bow }, b] };
    case 'spline':
      return { ...base, kind, points: [a, { x: mid.x + perp.x * bow, y: mid.y + perp.y * bow }, b] };
    case 'splineHeading':
      return { ...base, kind, points: [a, b], headingStart: dir, headingEnd: null };
    case 'boomerang':
      return { ...base, kind, points: [a, b], arrows: 'none', boom: defaultBoom(dir, dir) };
  }
}

export function createShape(shape: 'circle' | 'rect', x: number, y: number, w: number, h: number): ShapeEl {
  return {
    id: uid(),
    type: 'shape',
    shape,
    x,
    y,
    w,
    h,
    rotation: 0,
    fill: 'none',
    stroke: INK,
    strokeWidth: 1.4,
    dash: 'solid',
    opacity: 1
  };
}

export function createText(x: number, y: number, text = 'Text'): TextEl {
  return {
    id: uid(),
    type: 'text',
    x,
    y,
    text,
    size: 7,
    color: INK,
    bold: false,
    italic: false,
    align: 'left',
    rotation: 0,
    opacity: 1
  };
}

export function createStamp(kind: StampKind, x: number, y: number): StampEl {
  return {
    id: uid(),
    type: 'stamp',
    kind,
    x,
    y,
    w: GOAL_FOOTPRINT_IN,
    h: GOAL_FOOTPRINT_IN,
    rotation: 0,
    opacity: 1
  };
}
