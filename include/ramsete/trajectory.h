#pragma once

#include <vector>

// trajectory — the baked schedule and how to read it: where to be, facing which way, moving how fast, at every instant. The follower only asks "what should I be doing at time t?".

// One baked row. Rows sit ProfileConfig.dt apart on the clock (10 ms by default), so they are evenly spaced in TIME and crowd up in DISTANCE wherever the robot is slow.
struct TrajPoint {
    double t       = 0;   // seconds since the trajectory started
    double x       = 0;   // inches, field frame
    double y       = 0;
    double heading = 0;   // degrees, 0 = +Y, CW+ — the tangent, flipped 180° on reversed rows
    double v       = 0;   // in/s, signed: negative means this row is driven backward
    double ω       = 0;   // deg/s, = |v| · curvature (the sign comes from the curvature alone)
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

double trajectoryDuration(const std::vector<TrajPoint>& traj);

// Lerp between the two rows bracketing t, clamped to the ends. Heading interpolates along the WRAPPED difference so a pair straddling ±180° doesn't spin the long way.
TrajState sampleTrajectory(const std::vector<TrajPoint>& traj, double t);
