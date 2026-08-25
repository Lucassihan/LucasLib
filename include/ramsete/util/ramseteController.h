#pragma once

#include "ramsete/util/bezierUtils.h"
#include "ramsete/util/trajectoryUtils.h"
#include "ramsete/util/profile.h"
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
    double maxVel;          // in/s — the CHASSIS's free speed, never a route's; cap PathSettings instead or the clock thinks it has no headroom
    double maxAccel;        // in/s² — the profile's acceleration budget
    double sampleRate;      // MS between baked rows, not a frequency; PathSettings.dt overrides it per route

    // --- smoothing on the measurements the velocity layer reads back ---
    // chassis.v and chassis.ω are each one interval of pose difference, and the pose arrives in
    // bursts, so a single reading is noisy enough to move the drive a couple of volts once the
    // velocity channel's kP is on top of it. Each channel is set on its own: v gains far more from
    // averaging than ω does, and ω pays more for the delay, since it swings faster.
    // Samples run 1..5; 1 is the same as off. Costs (samples-1)/2 cycles of delay.
    bool   smoothV;
    int    smoothedVSamples;
    bool   smoothω;
    int    smoothedωSamples;

    // --- the law ---
    double b;               // stiffness, 1/in²
    double zeta;            // damping, (0,1): smaller is snappier and rings sooner
    double vMinGain;        // in/s floor inside k, so the gain survives v_d → 0

    // --- the clock ---
    double clockCoupling;   // how hard a speed shortfall brakes the clock

    // --- output slew ---
    // A rate cap on the COMMAND, applied after the law and ahead of the velocity layer. The
    // profile's maxAccel shapes the schedule; these bound how fast the law's feedback terms are
    // allowed to move on top of it, so a catch-up spike reaches the drivetrain as a ramp. Real
    // units, not per-tick deltas. 0 leaves that half uncapped.
    double vMaxAccelRate;   // in/s²  — commanded v, magnitude growing
    double vMaxDecelRate;   // in/s²  — commanded v, magnitude shrinking
    double ωMaxAccelRate;   // deg/s² — commanded ω, magnitude growing
    double ωMaxDecelRate;   // deg/s² — commanded ω, magnitude shrinking

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

    // Steer toward a baked SCHEDULE rather than toward a point. Takes the rows themselves, so a
    // route can be built once — createTrajectory(path) in initialize(), kept in a
    // Trajectory — and then driven as often as you like for the cost of the drive alone.
    // Ends when the reference clock passes the last row, with the exit conditions as the safety
    // bound, so a baked endSpeed leaves the motors running for the next movement to catch — unless
    // the schedule's own finalDistance hands the last inches to the point chase first, which is the
    // only half of this that can settle. Direction is baked in too.
    void trajectory(const Trajectory& trajectory,
                    LateralExitConditions ec = {});

    // ------------------------------------------------------------ boomerangTo ---

    // One curve from where the robot is standing to (x, y), then followed like a trajectory.
    // The start anchor sits on the robot with its handle along the direction of TRAVEL; the end
    // anchor sits on (x, y) with its handle along `heading`, which is a field heading in LucasLib's
    // convention (0 = +Y, clockwise) and is NOT flipped by `backward`. The two handle lengths set
    // how hard the curve commits to leaving and to arriving — longer is a wider, lazier arc.
    // `padding` ends the curve that many inches short, on the approach line, and drives the rest
    // dead straight: the robot is done turning before it arrives. 0 is the plain single curve.
    void boomerangTo(double x, double y, double heading,
                     double handleLength1, double handleLength2, double padding = 0,
                     PathSettings pathSettings = {},
                     LateralExitConditions ec = {});
};

extern Ramsete ramsete;
