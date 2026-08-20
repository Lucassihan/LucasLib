#pragma once
#include "vex.h"
#include "odometry.h"
#include "coreConfigs.h"
#include "pid.h"

struct LateralExitConditions {
    double t   = 5000;
    double v   = 2;
    double d   = -1;

    double settleWithin = 12;
};

struct TurnExitConditions {
    double t = 5000;
    double v = 0.35;
    double d = -1;

    double settleWithin = 20;
};

bool checkLateralExit(LateralExitConditions& ec, double startTime,
                      bool hasPointTarget = true);

bool checkTurnExit(TurnExitConditions& ec, double startTime);
