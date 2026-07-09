#pragma once
#include "vex.h"
#include "odometry.h"
#include "coreConfigs.h"
#include "pid.h"

//These are initiated as parameters inside movement functions in movement.cpp.
struct LateralExitConditions {
    double tOut  = 5000;   // timeout, 5000 ms by default, 0 = off
    double setD  = 2;      // settle: exit once |D term| < setD inside the exit zone
    double extD  = 2;      // exit distance — inches from target, 0 = disabled
    double pLine = 0;      // perpendicular-line exit distance, inches. 0 = disabled.
                           //   The line runs through the target ⊥ to the robot's heading.
                           //   Arms once the robot is within extD of the target, then exits
                           //   when the robot's distance to that line drops to pLine.
};

struct TurnExitConditions {
    double tOut = 5000;  // timeout, 5000 ms by default, 0 = off
    double setD = 2;     // settle: exit once |D term| < setD inside the exit zone
    double extA = 10;    // exit angle — degrees from target, 0 = disabled
};

struct ExitStateVars {
    double startTime      = Brain.timer(msec);
    bool   perpLineActive = false;  // latches true once robot enters extD activation radius
};

void resetExitStateVars(ExitStateVars& exitStateVars);

bool checkLateralExit(LateralExitConditions& ec, ExitStateVars& exitStateVars,
                      double driveError,
                      double targetX, double targetY,
                      bool backward = false);

bool checkTurnExit(TurnExitConditions& ec, ExitStateVars& exitStateVars,
                   double turnError);
