#pragma once

#include <cstddef>
#include <vector>

// trajectory — the baked schedule and how to read it: where to be, facing which way, moving how fast, at every instant. The follower only asks "what should I be doing at time t?".

// One baked row. Rows sit PathSettings.dt apart on the clock (10 ms by default), so they are evenly spaced in TIME and crowd up in DISTANCE wherever the robot is slow.
struct TrajPoint {
    double t       = 0;   // seconds since the trajectory started
    double x       = 0;   // inches, field frame
    double y       = 0;
    double heading = 0;   // degrees, 0 = +Y, CW+ — the tangent, flipped 180° on reversed rows
    double v       = 0;   // in/s, signed: negative means this row is driven backward
    double ω       = 0;   // deg/s, = |v| · curvature (the sign comes from the curvature alone)
};

// The schedule itself: the rows in order, plus the one follow-time setting the bake is asked for.
// One TrajPoint is one INSTANT of it, so the trajectory is the whole thing, not any single element.
// The accessors forward to the rows so it reads like the vector it mostly is.
struct Trajectory {
    std::vector<TrajPoint> rows;
    double finalDistance = 6;   // in — carried from the PathSettings it was baked with

    // The last stretch is a straight run onto the final row, and the follower drives it the way
    // driveForward would: drivePID on the distance left, headingPID holding the last row's heading.
    // Once the REFERENCE is this close to the end the law is off, so nothing is steering to a curve
    // that no longer bends. 0 leaves the law in charge the whole way. ramsete.boomerangTo sets it
    // from its `padding`; a hand-baked path that ends straight can set it too.
    double straightTail  = 0;   // in

    std::size_t      size()               const { return rows.size(); }
    bool             empty()              const { return rows.empty(); }
    const TrajPoint& operator[](std::size_t i) const { return rows[i]; }
    const TrajPoint& front()              const { return rows.front(); }
    const TrajPoint& back()               const { return rows.back(); }
    const TrajPoint* begin()              const { return rows.data(); }
    const TrajPoint* end()                const { return rows.data() + rows.size(); }
};

// The reference state at one instant: a row interpolated to the current clock.
struct TrajState {
    double x       = 0;
    double y       = 0;
    double heading = 0;
    double v       = 0;
    double ω       = 0;
};

// Folds an angle into (-180, 180].
double wrapDegrees(double angle);

double trajectoryDuration(const Trajectory& traj);

// The clock time at which the last `tail` inches of a schedule begin — where a follower that drives
// the straight run itself should take over. Two things decide it, and both are about not switching
// controllers at a bad moment:
//
//   * the arc is walked back along the ROWS, never taken as a distance to the last one. A path that
//     swings past its own target before turning in on it is euclidean-close to the finish long
//     before it is arc-close, and would hand over with most of the curve still unflown.
//   * the answer is never earlier than the profile's fastest row. Everything before that is still
//     speeding up, and a path short enough to fit inside the tail would otherwise hand over on row
//     zero — at the standing start, the one place the schedule really is at rest.
//
// So the handoff always lands on the braking side of the profile, with the robot still moving.
// Returns -1 when there is no usable handoff: no tail asked for, or too few rows to find one.
double trajectoryTailStart(const Trajectory& traj, double tail);

// Lerp between the two rows bracketing t, clamped to the ends. Heading interpolates along the WRAPPED difference so a pair straddling ±180° doesn't spin the long way.
TrajState sampleTrajectory(const Trajectory& traj, double t);
