#pragma once
#include "vex.h"
#include "odometry.h"
#include "coreConfigs.h"

//These are initiated as parameters inside movement functions in movement.cpp.
struct LateralExitConditions {
    double timeout   = 5000;   // 5000 ms by default, 0 = off
    bool   settle    = false;
    double smallError = 1;   // inches — exit after staying within this for smallTime ms continuously
    double smallTime  = 100;   // ms
    double bigError   = 3;   // inches — exit after staying within this for bigTime ms continuously
    double bigTime    = 500;   // ms
    double exitDist  = 0;      // inches from target, 0 = disabled
    double perpLine  = 0;      // inches — activation radius around target. 0 = disabled.
                               //   Once robot enters this radius, the perpendicular-line
                               //   check latches on and exits when the target falls behind
                               //   the robot's heading axis (or in front, for backward moves).
};

struct TurnExitConditions {
    double timeout    = 5000;  // 5000 ms by default, 0 = off
    bool   settle     = false;
    double smallError = 1;   // degrees — exit after staying within this for smallTime ms continuously
    double smallTime  = 150;   // ms
    double bigError   = 3;   // degrees — exit after staying within this for bigTime ms continuously
    double bigTime    = 400;   // ms
    double exitAng    = 0;     // degrees from target, 0 = disabled
    double exitPct    = 0;     // ratio of total angle turned (0-1), 0 = disabled
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
