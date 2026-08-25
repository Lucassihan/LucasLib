#pragma once

#include "ramsete/util/bezierUtils.h"
#include "ramsete/util/trajectoryUtils.h"

#include <vector>

// profile — turns a path into a schedule under three velocity ceilings: outer-wheel saturation v_max/(1 + |k|T/2), the authored per-segment maxVel, and a torque-aware acceleration sweep run forward then backward.
// Plan-time work on plain arrays; a few milliseconds, so it can run on the brain at the top of an auton.

struct PathSettings {
    // This route's ceiling, not the chassis's. -1 = fall back to ramsete.maxVel.
    double maxVel          = -1;    // in/s
    double maxAccel        = -1;    // in/s²   (-1 = ramsete.maxAccel)
    double maxDecel        = -1;    // in/s²   (-1 = maxAccel)
    double trackwidth      = -1;    // in      (-1 = ramsete.trackwidth)

    // Fraction of maxVel the ceilings may ask for; the rest is headroom for the feedback terms, since a saturated outer wheel is an open loop.
    double velMargin       = 0.9;

    double startSpeed      = 0;     // in/s in the first row (bake a hot entry)
    double endSpeed        = 0;     // in/s in the last row  (bake a hot exit)

    // Milliseconds of schedule between rows. Rows land on a clock, not on a distance, so they no
    // longer crowd up wherever the profile is slow — a row costs the same everywhere. The follower
    // interpolates either way, so this trades trace size against how finely a joint or a brake ramp
    // resolves. -1 = fall back to ramsete.sampleRate, the same way the three above do.
    double dt              = -1;    // ms

    // Normalized motor torque curve at the current speed fraction: tau(f) = max(torqueFloor, 1 - torqueSlope·f).
    double torqueSlope     = 0.8;
    double torqueFloor     = 0.15;

    // Where the follower stops tracking the schedule and starts parking. The law follows a MOVING
    // reference, so it has nothing to hold position with once the rows run out; moveTo's law is the
    // same one with the reference standing still, which is what actually settles the robot. Inside
    // this many inches of the last row the movement hands over, and the exit conditions take it from
    // there. 0 = never hand over, which leaves the schedule as the only finish line.
    // Ignored on a hot exit: a nonzero endSpeed means the robot is meant to still be moving.
    double finalDistance   = 6;     // in
};

// Bakes a spline into a schedule. Direction flips between segments, and any segment marked stopEnd, become forced v = 0 cusps.
Trajectory createTrajectory(const std::vector<BezierSegment>& path,
                            PathSettings pathSettings = {},
                            bool backward = false);
