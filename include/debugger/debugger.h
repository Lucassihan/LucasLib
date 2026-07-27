#pragma once

// =========================================================================
// debugger — trace capture for the LucasLib Debugger app.
//
// Records movements + a 10ms-cadence tick stream into RAM during an auton,
// then dumps a LUCASLIB_TRACE the app can import. Designed to be invisible:
//   • one debugger::start()/stop() pair around the auton,
//   • one tag line at the top of each movement (already wired in movement.cpp),
//   • a single hook inside voltageDrive() does all the per-tick sampling.
// Everything is a no-op until start() is called, so it costs nothing when off.
//
// Console hygiene: the dump prefixes every line with a marker and mutes the
// brain/controller screen tasks for its duration, so scrambled screen text on
// the terminal can't corrupt the trace. The app strips the marker on import.
// =========================================================================

#include "movement/exitConditions.h"
#include "movement/movementUtils.h"   // BoomParams

class PID;  // forward decl — the debugger keeps PID internals in its own table

namespace debugger {

// ---- Lifecycle: call once around the auton ----
// start() snapshots the header (pose, battery, trackwidth, gains, governors)
// and arms capture. stop() auto-dumps to the console (no input needed) once
// minSecondsSinceStart have elapsed. It prints the trace SEVERAL times back to
// back: VEXcode re-opens the USB port mid-stream so the first copy is partial,
// but the rest stream complete and the importer keeps only the last complete
// one. stop("trace.txt") instead writes to the microSD card (single clean copy).
void start(const char* autonName);
void stop(double minSecondsSinceStart = 3.0);
void stop(const char* filename);

// ---- Movement tags: one line at the top of each movement ----
// (Already placed in movement.cpp — you shouldn't need to touch these.)
void moveTurn(const char* type, double targetHeading,
              const TurnExitConditions& ec, double partiality = 0);
void movePointTurn(const char* type, double tx, double ty,
                   const TurnExitConditions& ec, double partiality = 0);
void moveStraight(const char* type, double distance, double heading,
                  const LateralExitConditions& ec, bool backward);
void movePoint(const char* type, double tx, double ty,
               const LateralExitConditions& ec, bool backward);
void moveLyPose(double tx, double ty, double pathHeading, double turnDisable,
                const LateralExitConditions& ec, bool backward);
void moveBoomerang(const BoomParams& bp, const LateralExitConditions& ec);
void moveVoltage(double left, double right);

// ---- Per-tick sampling (hooked inside voltageDrive); no-op unless capturing ----
void tick();

// ---- PID internals mirror (one line at the end of PID::calculateOutput) ----
// Hands the debugger a PID instance's running integral + locked gains so nothing
// debugger-only has to live on the PID class. Keyed by instance. No-op off-capture.
void samplePID(const PID* pid, double integral, double kP, double kI, double kD);

// ---- Overlay telemetry, published once per loop by the lyPose / boomerang
//      controllers right before voltageDrive (consumed by the next tick) ----
void probeLyapunov(double ex, double ey);
void probeBoomerang(double carrotX, double carrotY, double ghostX, double ghostY,
                    double leadRetraction, double maxSlip, int stage);

// ---- Read-wall correction hook (from Odometry::readWall) ----
// Call readWallBefore() right before the setPos() correction, then readWall()
// right after: the debugger snapshots the pre-pose itself and pairs it with the
// corrected pose, so no before/after locals leak into the odometry code.
void readWallBefore();
void readWall(int wall, int sensor);

// ---- Screen tasks check this so the dump can take the console cleanly ----
bool screensMuted();

} // namespace debugger
