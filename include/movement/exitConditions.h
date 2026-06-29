#pragma once
#include "vex.h"
#include "odometry.h"
#include "coreConfigs.h"
#include "pid.h"

//These are initiated as parameters inside movement functions in movement.cpp.
struct LateralExitConditions {
    double timeout   = 5000;   // 5000 ms by default, 0 = off
    double settleD = 2;
    double exitDist  = 2;      // inches from target, 0 = disabled
    double perpLine  = 0;      // inches — activation radius around target. 0 = disabled.
                               //   Once robot enters this radius, the perpendicular-line
                               //   check latches on and exits when the target falls behind
                               //   the robot's heading axis (or in front, for backward moves).
};

struct TurnExitConditions {
    double timeout    = 5000;  // 5000 ms by default, 0 = off
    double settleD = 2;
    double exitAng    = 10;     // degrees from target, 0 = disabled
};

struct ExitStateVars {
    double startTime        = Brain.timer(msec);
    bool   perpLineActive   = false;  // latches true once robot enters perpLine activation radius
    double smallSettleStart = 0;      // 0 = not currently in small window
    double bigSettleStart   = 0;      // 0 = not currently in big window
};

void resetExitStateVars(ExitStateVars& exitStateVars);

bool checkLateralExit(LateralExitConditions& ec, ExitStateVars& exitStateVars,
                      double driveError,
                      double targetX, double targetY,
                      bool backward = false);

bool checkTurnExit(TurnExitConditions& ec, ExitStateVars& exitStateVars,
                   double turnError);
