import type { DriveWheelCount, IntakePosition, SixWheelLayout } from '$lib/geom/chassis';

export interface Pt {
  x: number;
  y: number;
}

export type Dash = 'solid' | 'dashed' | 'dotted';
export type Arrows = 'none' | 'start' | 'end' | 'both';

export interface RobotChassis {
  widthHoles: number;
  lengthHoles: number;
  driveWheelCount: DriveWheelCount;
  sixWheelLayout: SixWheelLayout;
  intakePosition: IntakePosition;
  trackingCenterIndex: number;
}

interface Base {
  id: string;
  opacity: number; // 0..1
  name?: string;
  locked?: boolean;
}

export interface RobotEl extends Base {
  type: 'robot';
  x: number;
  y: number;
  heading: number; // degrees, field convention (0 = +Y up, CW positive)
  chassis: RobotChassis;
  color: string;
  fill: boolean; // tint the body
  label?: string;
}

export type PathKind = 'straight' | 'arc' | 'spline' | 'splineHeading' | 'boomerang';

export interface BoomParams {
  lead: number; // coefficient of the initial robot→target distance
  gLead: number; // ghost lead (0..1); how far the ghost trails the carrot
  startHeading: number; // degrees, initial robot facing
  targetHeading: number; // degrees, arrival facing
  backward: boolean;
  showLeads: boolean; // draw the lead axis + carrot/ghost anchors
  robotT: number | null; // 0..1 scrub position for a robot on the path; null = hidden
  chassis: RobotChassis;
}

export interface PathEl extends Base {
  type: 'path';
  kind: PathKind;
  points: Pt[]; // control points (field coords); meaning depends on kind
  headingStart?: number; // splineHeading: fixed tangent heading at first point (deg)
  headingEnd?: number | null; // splineHeading: optional fixed tangent at last point
  boom?: BoomParams; // only for kind === 'boomerang'
  color: string;
  width: number; // stroke width in inches
  dash: Dash;
  arrows: Arrows;
}

export interface ShapeEl extends Base {
  type: 'shape';
  shape: 'circle' | 'rect';
  x: number; // center
  y: number;
  w: number; // circle: diameter
  h: number;
  rotation: number; // deg (rect only, visually)
  fill: string | 'none';
  stroke: string;
  strokeWidth: number;
  dash: Dash;
}

export interface TextEl extends Base {
  type: 'text';
  x: number;
  y: number;
  text: string;
  size: number; // world units (inches)
  color: string;
  bold: boolean;
  italic: boolean;
  align: 'left' | 'center' | 'right';
  rotation: number; // deg
}

export type StampKind = 'goal-blue' | 'goal-red';

export interface StampEl extends Base {
  type: 'stamp';
  kind: StampKind;
  x: number; // center
  y: number;
  w: number; // real footprint inches
  h: number;
  rotation: number; // deg
}

export type Element = RobotEl | PathEl | ShapeEl | TextEl | StampEl;
export type ElementType = Element['type'];

export interface Diagram {
  id: string;
  name: string;
  elements: Element[];
}
