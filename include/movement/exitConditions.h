#pragma once
#include "vex.h"
#include "odometry.h"
#include "coreConfigs.h"
#include "pid.h"

// These are initiated as parameters inside movement functions in movement.cpp.
// Two independent ways to end a lateral move — settle and cross — each with its
// OWN activation radius, so a small settle zone and a big cross zone don't fight.
struct LateralExitConditions {
    double tOut   = 5000;  // timeout, ms · 0 = off

    // SETTLE — "arrived and stopped near the target". Disabled with setR = 0.
    double setR   = 10;    // settle activation radius, in — settle only counts inside setR · 0 = off
    double setD   = 2;     // exit once |D term| < setD inside setR (bigger = looser / exits sooner)

    // CROSS — "passed the target's perpendicular line". Disabled with crossD < 0.
    // The line is infinite and runs ⊥ to the robot's heading through the target.
    double crossD = -1;    // slide the line this many in TOWARD us to fire early · 0 = line sits
                           // on the target · negative = off
};

// Same shape as the lateral struct: SETTLE owns its activation zone, and a second
// exit runs independently of it.
struct TurnExitConditions {
    double tOut = 5000;  // timeout, ms · 0 = off

    // SETTLE — "arrived and stopped on the heading". Disabled with setR = 0.
    double setR = 10;    // settle activation angle, deg — settle only counts inside setR · 0 = off
    double setD = 2;     // exit once |D term| < setD inside setR (bigger = looser / exits sooner)

    // ANGLE — "close enough, don't wait for it to stop". Independent of settle:
    // no derivative gate, so it fires mid-rotation the moment the error is small.
    double extA = 0;     // exit once |error| <= extA · 0 = off
};

// startTime = the move's start timestamp (Brain.timer(msec)), captured by the caller
// before the control loop and passed in for the timeout check.
bool checkLateralExit(LateralExitConditions& ec, double startTime,
                      double driveError,
                      double targetX, double targetY,
                      bool backward = false);

bool checkTurnExit(TurnExitConditions& ec, double startTime,
                   double turnError);
