// Parses LucasLib trace text into a typed Trace.
//
// Format: `#key=value` header lines, then [MOVEMENTS] / [TICKS] / [READWALLS]
// sections, each a CSV with a header row. Columns are read by NAME, so column
// order and future additions don't break parsing.

import type { Trace, TraceHeader, Movement, Tick, ReadWall } from './types';

type Row = Record<string, string>;

// On-robot dumps prefix every trace line with this marker so the importer can
// pull the trace out of a noisy console (scrambled brain/controller text, etc.).
const TRACE_MARKER = '~LLT|';

// If the text contains marker-prefixed lines (a console dump), keep ONLY those
// and strip the marker — discarding any interleaved console noise. Otherwise
// (a clean generated trace) return the text untouched.
export function stripMarkers(text: string): string {
  if (!text.includes(TRACE_MARKER)) return text;
  const out: string[] = [];
  for (const line of text.split(/\r?\n/)) {
    const i = line.indexOf(TRACE_MARKER);
    if (i >= 0) out.push(line.slice(i + TRACE_MARKER.length));
  }
  return out.join('\n');
}

function splitSections(text: string) {
  const headerLines: string[] = [];
  const sections: Record<string, string[]> = {};
  let current: string | null = null;

  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (line === '') continue;

    const marker = line.match(/^\[(\w+)\]$/);
    if (marker) {
      current = marker[1];
      sections[current] = [];
      continue;
    }

    if (current === null) {
      headerLines.push(line);
    } else if (line.startsWith('#')) {
      // comment / end sentinel (e.g. #END) inside a section — ignore
      continue;
    } else {
      sections[current].push(line);
    }
  }

  return { headerLines, sections };
}

function parseHeader(headerLines: string[]): TraceHeader {
  const kv: Record<string, string> = {};
  let version = 'unknown';

  for (const line of headerLines) {
    if (!line.startsWith('#')) continue;
    const body = line.slice(1);
    if (body.startsWith('LUCASLIB_TRACE')) {
      version = body.split(/\s+/)[1] ?? 'unknown';
      continue;
    }
    const eq = body.indexOf('=');
    if (eq >= 0) {
      kv[body.slice(0, eq).trim()] = body.slice(eq + 1).trim();
    }
  }

  const pose = (kv['start_pose'] ?? '0,0,0').split(',').map(Number);

  const gov = (scope: string, maxDefault: number) => ({
    minOutput: Number(kv[`gov_${scope}_min`] ?? 0),
    maxOutput: Number(kv[`gov_${scope}_max`] ?? maxDefault),
    maxAccelRate: Number(kv[`gov_${scope}_accel`] ?? 0),
    maxDecelRate: Number(kv[`gov_${scope}_decel`] ?? 0)
  });

  return {
    version,
    autonName: kv['auton_name'] ?? 'unnamed',
    startPose: { x: pose[0] || 0, y: pose[1] || 0, h: pose[2] || 0 },
    trackwidth: Number(kv['trackwidth'] ?? 0),
    headingDisableDist: Number(kv['headingDisableDist'] ?? 6),
    useLyapunov: kv['useLyapunov'] === '1',
    kLat: Number(kv['kLat'] ?? 0),
    govern: {
      total: gov('total', 12),
      drive: gov('drive', 12),
      turn: gov('turn', 12)
    }
  };
}

function toRows(sectionLines: string[]): Row[] {
  if (!sectionLines || sectionLines.length === 0) return [];
  const cols = sectionLines[0].split(',').map((s) => s.trim());
  const rows: Row[] = [];
  for (let i = 1; i < sectionLines.length; i++) {
    const vals = sectionLines[i].split(',');
    const row: Row = {};
    cols.forEach((c, idx) => (row[c] = (vals[idx] ?? '').trim()));
    rows.push(row);
  }
  return rows;
}

const num = (r: Row, k: string) => Number(r[k] ?? 0) || 0;
const bool = (r: Row, k: string) => r[k] === '1' || r[k]?.toLowerCase() === 'true';

function parseMovements(rows: Row[]): Movement[] {
  return rows.map((r) => ({
    index: num(r, 'index'),
    type: r['type'] ?? 'unknown',
    targetX: num(r, 'targetX'),
    targetY: num(r, 'targetY'),
    targetH: num(r, 'targetH'),
    distance: num(r, 'distance'),
    backwards: bool(r, 'backwards'),
    direction: num(r, 'direction'),
    partiality: num(r, 'partiality'),
    dLead: num(r, 'dLead'),
    n: num(r, 'n'),
    gLead: num(r, 'gLead'),
    leadChasePowerLimit: num(r, 'leadChasePowerLimit'),
    pathHeading: num(r, 'pathHeading'),
    turnDisableLength: num(r, 'turnDisableLength'),
    leftVolts: num(r, 'leftVolts'),
    rightVolts: num(r, 'rightVolts'),
    heading: num(r, 'heading'),
    cosineScaling: bool(r, 'cosineScaling'),
    turnPriority: num(r, 'turnPriority'),
    exitTimeout: num(r, 'exitTimeout'),
    exitSmallError: num(r, 'exitSmallError'),
    exitSmallTime: num(r, 'exitSmallTime'),
    exitBigError: num(r, 'exitBigError'),
    exitBigTime: num(r, 'exitBigTime'),
    exitDist: num(r, 'exitDist'),
    exitPerpLine: num(r, 'exitPerpLine'),
    exitAng: num(r, 'exitAng'),
    exitPct: num(r, 'exitPct'),
    exitReason: r['exitReason'] ?? '',
    startTime: num(r, 'startTime'),
    endTime: num(r, 'endTime'),
    driveKp: num(r, 'driveKp'),
    driveKi: num(r, 'driveKi'),
    driveKd: num(r, 'driveKd'),
    turnKp: num(r, 'turnKp'),
    turnKi: num(r, 'turnKi'),
    turnKd: num(r, 'turnKd')
  }));
}

function parseTicks(rows: Row[]): Tick[] {
  return rows.map((r) => ({
    t: num(r, 't'),
    mi: num(r, 'mi'),
    x: num(r, 'x'),
    y: num(r, 'y'),
    h: num(r, 'h'),
    stage: num(r, 'stage'),
    lateralError: num(r, 'le'),
    headingError: num(r, 'he'),
    drivePow: num(r, 'dp'),
    headingPow: num(r, 'hp'),
    turnPow: num(r, 'tp'),
    swingPow: num(r, 'sp'),
    driveOut: num(r, 'do'),
    turnOut: num(r, 'to'),
    leftOut: num(r, 'lo'),
    rightOut: num(r, 'ro'),
    yInches: num(r, 'yi'),
    xInches: num(r, 'xi'),
    leadX: num(r, 'leadX'),
    leadY: num(r, 'leadY'),
    leadRetraction: num(r, 'leadRetraction'),
    perpDist: num(r, 'perpDist'),
    carrotX: num(r, 'carrotX'),
    carrotY: num(r, 'carrotY'),
    ghostX: num(r, 'ghostX'),
    ghostY: num(r, 'ghostY'),
    arcRadius: num(r, 'arcRadius'),
    maxSlip: num(r, 'maxSlip'),
    ex: num(r, 'ex'),
    ey: num(r, 'ey'),
    driveI: num(r, 'driveI'),
    driveD: num(r, 'driveD'),
    turnI: num(r, 'turnI'),
    turnD: num(r, 'turnD')
  }));
}

function parseReadWalls(rows: Row[]): ReadWall[] {
  return rows.map((r) => ({
    t: num(r, 't'),
    mi: num(r, 'mi'),
    wall: num(r, 'wall'),
    sensor: num(r, 'sensor'),
    preX: num(r, 'preX'),
    preY: num(r, 'preY'),
    preH: num(r, 'preH'),
    postX: num(r, 'postX'),
    postY: num(r, 'postY'),
    postH: num(r, 'postH')
  }));
}

export function parseTrace(text: string): Trace {
  const { headerLines, sections } = splitSections(stripMarkers(text));
  const header = parseHeader(headerLines);
  const movements = parseMovements(toRows(sections['MOVEMENTS']));
  const ticks = parseTicks(toRows(sections['TICKS']));
  const readWalls = parseReadWalls(toRows(sections['READWALLS']));

  const duration = ticks.length ? ticks[ticks.length - 1].t : 0;

  return { header, movements, ticks, readWalls, duration };
}
