#pragma once

// Host-build stand-in for movement/odometry.h.
//
// The real Odometry drags in vex.h, which does not exist off the brain. The law
// only ever reads the tracked pose, so the harness supplies that much and writes
// its simulated robot into `chassis` before each tick.

struct Odometry {
    double x = 0;
    double y = 0;
    double h = 0;
};

extern Odometry chassis;
