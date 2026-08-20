#pragma once

// Host-build stand-in for movement/exitConditions.h.
//
// ramseteController.h names LateralExitConditions in moveTo's signature, and the
// real header reaches vex.h through odometry.h and pid.h. Nothing the harness
// checks ever runs an exit condition, so the shape of the struct is the whole of
// what it needs. Kept in step with the real defaults so a check that reads one
// is reading the same number the robot would.

struct LateralExitConditions {
    double t = 5000;
    double v = 2;
    double d = -1;

    double settleWithin = 12;
};
