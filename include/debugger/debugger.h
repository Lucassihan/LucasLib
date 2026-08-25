#pragma once

// debugger — records movements and a 10ms tick stream into RAM during an auton, then dumps a LUCASLIB_TRACE for the Debugger app. No-op until start().

#include "movement/exitConditions.h"
#include "movement/movementUtils.h"   // BoomParams
#include "ramsete/util/trajectoryUtils.h"       // TrajPoint

#include <vector>

class PID;  // the debugger keeps PID internals in its own table

namespace debugger {

// ---- Lifecycle: call once around the auton ----
void start(const char* autonName);
// Writes to the microSD as <autonName>.txt, the name start() was given. Never
// overwrites: an existing traj.txt pushes the new one to traj2.txt, traj3.txt…
// With no card it falls back to dumping over the console so the run isn't lost.
void stop();

// ---- Movement tags: already placed in movement.cpp ----
void moveTurn(const char* type, double targetHeading,
              const TurnExitConditions& ec, double partiality = 0);
void movePointTurn(const char* type, double tx, double ty,
                   const TurnExitConditions& ec, double partiality = 0);
void moveStraight(const char* type, double distance, double heading,
                  const LateralExitConditions& ec, bool backward);
// cosineScaling is what this movement hands blendOutputs, not what it does with the cosine: ramsete.moveTo folds it into drivePow and blends with the stage OFF.
void movePoint(const char* type, double tx, double ty,
               const LateralExitConditions& ec, bool backward,
               bool cosineScaling = true);
void moveBoomerang(const BoomParams& bp, const LateralExitConditions& ec);

// Names the NEXT movement the debugger opens, overriding the type that tag passes.
// A movement built on top of another one — ramsete.boomerangTo is a bake handed straight to
// ramsete.trajectory — would otherwise be traced as the movement it delegated to, and the only
// alternative is a trace-only argument threaded through the follow loop's signature. Consumed by
// the first tag after it, dropped if none comes. `type` must outlive the trace (a string literal).
void nextMoveType(const char* type);
void moveVoltage(double left, double right);

// Overrides the open movement's exit reason with what ACTUALLY ended it.
// The reason recorded when a movement starts is a guess read off the ARMED exit
// conditions, so it can't name an exit those conditions don't own —
// ramsete.trajectory normally ends on its reference clock reaching the last row,
// which isn't an exit condition at all. `reason` must outlive the trace (a string
// literal), the same contract the movement type has.
void movementExit(const char* reason);

// The whole baked schedule behind the current ramsete.trajectory, recorded once
// before the follow loop and attached to the movement that is open now.
// The per-tick refX/refY only trace the reference as far as the CLOCK got, at
// the tick rate — so a braked clock smears the shape and an early exit truncates
// it. The planned curve has to travel on its own to be drawn whole.
void trajectorySchedule(const Trajectory& traj);

// ---- Per-tick sampling, hooked inside voltageDrive ----
void tick();

// ---- PID internals mirror, one line at the end of PID::calculateOutput ----
// Keyed by instance, so nothing debugger-only has to live on the PID class.
void samplePID(const PID* pid, double integral, double kP, double kI, double kD);

// ---- Overlay telemetry, published by the boomerang controller right before voltageDrive ----
// Published by ramsete.trajectory every tick: the pose the schedule wanted.
// The app draws it as a ghost robot and trails it into the reference path.
void probeTrajectory();   // reads ramsete.telemetry directly — nothing to keep in sync

void probeBoomerang(double carrotX, double carrotY, double ghostX, double ghostY,
                    double leadRetraction, double maxSlip, int stage);

// ---- ramsete.moveTo's cross-track error ----
// The one number in MoveToError without a trace column; recomputing it app-side as le·sin(he) is only right while drivePID.targetOffset is 0.
void probeRamseteMove(double crossTrack);

// ---- Read-wall correction hook, from Odometry::readWall ----
// Call readWallBefore() right before the setPos() correction and readWall() right after, so no before/after locals leak into the odometry code.
void readWallBefore();
void readWall(int wall, int sensor);

// ---- Screen tasks check this so the dump can take the console cleanly ----
bool screensMuted();

} // namespace debugger
