#pragma once

#include "ramsete/bezierTools.h"
#include "ramsete/trajectory.h"
#include "movement/exitConditions.h"

#include <vector>

// ramseteController — the tracking law. k = 2·zeta·sqrt(ω_d² + b·v_d²), v = v_d·cos(e_h) + k·e_fwd, ω = ω_d + k·e_h + b·v_d·sinc(e_h)·e_cross.
// Frame: LucasLib is CW+ from +Y, so crossTrack is measured to the robot's RIGHT and ω stays CW+ — both chiralities flip, so the formula matches 2654E's textually. Flip both or neither.

// The pose error, seen from the robot's seat.
struct RamseteError {
    double forward    = 0;   // in — how far ahead the reference is along the nose
    double crossTrack = 0;   // in — how far to the robot's RIGHT it is
    double heading    = 0;   // deg — wrapped heading gap
};

// moveTo's error, measured off the DIRECTION OF TRAVEL (the tail on a backward move) rather than the nose.
struct MoveToError {
    double heading    = 0;   // deg — where the target sits, off the direction of travel. + is right.
    double distance   = 0;   // in  — signed. Negative on a backward move, and negative once overshot.
    double crossTrack = 0;   // in  — distance·sin(heading), sign paired with distance
};

// Published every tick by trajectory.
struct RamseteTelemetry {
    double refX       = 0;   // where the schedule says to be right now
    double refY       = 0;
    double refHeading = 0;
    double refV       = 0;
    double refω       = 0;

    double errX       = 0;   // robot-frame error: forward, right, heading gap
    double errY       = 0;
    double errHeading = 0;

    double k          = 0;   // the scheduled gain this tick
    double clock      = 0;   // reference time (adaptive-scaled)
    double realClock  = 0;   // real seconds since the movement started
    double clockRate  = 0;   // d_t, the throttle on reference time
    double duration   = 0;   // the schedule's total length

    double cmdV       = 0;   // what the law asked the drivetrain for
    double cmdω       = 0;
};

// Declared here, valued in config.cpp — all zero until setRamseteSettings() runs, and a zero maxVel stalls the profile.
struct Ramsete {
    // --- what the chassis can do (measured, not tuned) ---
    double trackwidth;      // in — wheel to wheel; sets the curvature speed cap
    double maxVel;          // in/s — the CHASSIS's free speed, never a route's; cap ProfileConfig instead or the clock thinks it has no headroom
    double maxAccel;        // in/s² — the profile's acceleration budget
    double sampleRate;      // MS between baked rows, not a frequency; ProfileConfig.dt overrides it per route

    // --- the law ---
    double b;               // stiffness, 1/in²
    double zeta;            // damping, (0,1): smaller is snappier and rings sooner
    double vMinGain;        // in/s floor inside k, so the gain survives v_d → 0

    // --- the clock ---
    double clockCoupling;   // how hard a speed shortfall brakes the clock

    // --- moveTo ---
    double kLat;            // 1/in — cross-track coupling. Not b's equal: this multiplies drivePID's volts, b multiplies in/s. Tuned separately.

    RamseteTelemetry telemetry;

    // Odometry pose minus the reference, rotated into the robot's axes.
    RamseteError error(const TrajState& referenceState);

    // The scheduled gain, shared by every feedback term.
    double gain(const TrajState& referenceState);

    // Reference-clock throttle in ~[0, 1]: |cos(e_heading)| times a brake that engages once catch-up demand outruns spare speed. clockCoupling = 0 drops the brake, not the facing factor.
    double clockRate(const RamseteError& e, const TrajState& referenceState, double k);

    // Forward half of the law. clockRate is unused, taken only so both halves are called the same way.
    double calculateTargetV(const TrajState& referenceState, const RamseteError& e,
                            double k, double clockRate);

    // Steering half. A braked clock slows the scheduled turning too, so the reference degrades into a pace car instead of driving away.
    double calculateTargetω(const TrajState& referenceState, const RamseteError& e,
                            double k, double clockRate);

    // ----------------------------------------------------------------- moveTo ---

    // Takes the pose by argument rather than reading `chassis`, so the host checks can fly it.
    MoveToError moveToError(double targetX, double targetY,
                            double robotX, double robotY, double robotH,
                            bool driveBackward);

    // kLat·v·e_y·sinc(e_θ), in volts. drivePower is the COSINE-SCALED output, the way lyPose fed it.
    double moveToTurn(const MoveToError& e, double drivePower);

    // Point chase on the lyapunov law — RAMSETE with a stopped reference. Honors the global `backward`, and runs on drivePID/headingPID in VOLTS rather than the velocity layer, which is why its gains sit with the pose movements'.
    void moveTo(double x, double y, LateralExitConditions ec = {});

    // ------------------------------------------------------------ trajectory ---

    // Steer toward a SCHEDULE rather than a point. Ends when the reference clock passes the last row, with the exit conditions as the safety bound, so a baked endSpeed leaves the motors running for the next movement to catch.
    void trajectory(const std::vector<TrajPoint>& trajectory,
                    LateralExitConditions ec = {.t = 10000, .v = -1});

    // Bake a spline on the brain (every ProfileConfig field at its default), then follow it. Costs a few ms up front.
    void trajectory(const std::vector<BezierSegment>& path,
                    LateralExitConditions ec = {.t = 10000, .v = -1});
};

extern Ramsete ramsete;
