// Decomposition of a ramsete.moveTo tick into the two halves of its steering
// law, shared by the readout, the ω graph and the field overlay.
//
// This is NOT lyPose's decomposition with the names changed — the two controllers
// differ in five places, and each one moves a number on screen:
//
//   1. Frame. lyPose committed a path line at entry and measured against it
//      forever; moveTo recomputes the bearing to the target every tick, from the
//      DIRECTION OF TRAVEL (the tail on a backward move).
//   2. States. lyPose's e_x and e_y were independent. Here there is no e_x, and
//      crossTrack = distance·sin(heading) by construction — one state, not two.
//   3. Sign. lyPose subtracted the coupling: ω = −(headingPID + kLat·v·e_y·sinc).
//      moveTo ADDS it: headingPow = headingPID(eθ) + kLat·v·sinc·e_y.
//   4. The v it multiplies. lyPose read the post-blend driveOut. moveTo passes
//      drivePow — cos(eθ)·drivePID(le), a SIGNED cosine, applied before the blend.
//      (blendOutputs runs with cosineScaling off here precisely because the
//      cosine is already in drivePow, so driveOut == drivePow. dp is the input
//      the law actually saw.)
//   5. Stages. The coupling is FAR-only. Inside headingDisableDist the movement
//      latches its heading, switches lateralError to the tracked straight-line
//      remainder, and the whole lyapunov term goes to zero.
//
// See src/ramsete/movement/moveTo.cpp and Ramsete::moveToTurn.

import type { Movement, Tick } from './types';
import { sinc, moveToTurn } from '$lib/sandbox/ramsete';

const D2R = Math.PI / 180;

/**
 * The tick's cross-track error.
 *
 * Traces recorded before the `xt` column existed fall back to distance·sin(eθ),
 * which reproduces it exactly whenever drivePID.targetOffset is 0 (its value on
 * every movement that doesn't deliberately set it). Meaningless in CLOSE either
 * way — the law doesn't read it there.
 */
export function crossTrackAt(tk: Tick): number {
  if (!Number.isNaN(tk.crossTrack)) return tk.crossTrack;
  return tk.lateralError * Math.sin(tk.headingError * D2R);
}

export interface MoveToLaw {
  /** stage 1 — heading latched, lateral error is the tracked remainder */
  close: boolean;
  heading: number; // eθ, deg
  lateral: number; // le, in — signed distance FAR, straight remainder CLOSE
  crossTrack: number; // e_y, in
  sincv: number; // sinc(eθ)
  /** -1 when the trace predates the header field — the term can't be split. */
  kLat: number;
  /** drivePID's own P/I/D terms, on le */
  dP: number;
  dI: number;
  dD: number;
  cosFactor: number; // cos(eθ) — signed, so a target behind the robot reverses
  drivePow: number; // v, volts — the cosine-scaled command the coupling multiplies
  headingPow: number; // ω, volts
  latTerm: number; // kLat·v·sinc(eθ)·e_y, volts. 0 in CLOSE.
  propTerm: number; // headingPID's governed output = ω − latTerm
}

export function moveToLaw(m: Movement, tk: Tick, kLat: number): MoveToLaw {
  const close = tk.stage >= 1;
  const crossTrack = crossTrackAt(tk);
  const sincv = sinc(tk.headingError * D2R);

  // FAR-only, and unknowable without kLat — an unsplit ω is better than a
  // split against a guessed constant.
  const latTerm =
    close || kLat < 0
      ? 0
      : moveToTurn(
          { heading: tk.headingError, distance: tk.lateralError, crossTrack },
          tk.drivePow,
          kLat
        );

  return {
    close,
    heading: tk.headingError,
    lateral: tk.lateralError,
    crossTrack,
    sincv,
    kLat,
    dP: m.driveKp * tk.lateralError,
    dI: m.driveKi * tk.driveI,
    dD: m.driveKd * tk.driveD,
    cosFactor: Math.cos(tk.headingError * D2R),
    drivePow: tk.drivePow,
    headingPow: tk.headingPow,
    latTerm,
    propTerm: tk.headingPow - latTerm
  };
}
