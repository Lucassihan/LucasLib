#pragma once
#include "vex.h"
#include "odometry.h"
#include "coreConfigs.h"
#include "pid.h"

// These are initiated as parameters inside movement functions in movement.cpp.
// Two independent ways to end a lateral move — settle and cross — each with its
// OWN activation radius, so a small settle zone and a big cross zone don't fight.
struct LateralExitConditions {
    double t   = 5000;
    double v   = 10;
    double d = -1;

    double settleGate = 12;
};

// Same shape as the lateral struct: SETTLE owns its activation zone, and a second
// exit runs independently of it.
struct TurnExitConditions {
    double t = 5000;
    double v = 10;
    double d = -1;

    double settleGate = 20;
};

// startTime = the move's start timestamp (Brain.timer(msec)), captured by the caller
// before the control loop and passed in for the timeout check.
//
// Everything else comes from the movement globals: lateralError for the settle gate,
// targetX/targetY for the cross line, and the global backward (read inside crossed()).
//
// hasPointTarget = false for movements that have no target POINT to cross — drive()
// is relative, so targetX/targetY are meaningless there and the cross test is skipped
// entirely rather than firing against the field origin. Every point/pose movement
// leaves it at the default.
bool checkLateralExit(LateralExitConditions& ec, double startTime,
                      bool hasPointTarget = true);

// Reads the global headingError rather than taking it as an argument — every turn
// movement writes that global every tick anyway.
bool checkTurnExit(TurnExitConditions& ec, double startTime);
