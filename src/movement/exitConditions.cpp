#include "movement/exitConditions.h"
#include "coreConfigs.h"

#include <cmath>

void resetExitStateVars(ExitStateVars& exitStateVars) {
    exitStateVars.startTime        = Brain.timer(msec);
    exitStateVars.smallSettleStart = 0;
    exitStateVars.bigSettleStart   = 0;
}

bool checkLateralExit(LateralExitConditions& ec, ExitStateVars& exitStateVars,
                      double driveError,
                      double targetX, double targetY,
                      bool backward) {

    double currentTime = Brain.timer(msec);

    // timeout check
    if (ec.timeout > 0) { //if enabled
        if (currentTime - exitStateVars.startTime >= ec.timeout) return true;
    }

    // exitDist + settle check
    if (ec.exitDist > 0) { //if enabled
        if (fabs(driveError) <= ec.exitDist) {
            if (ec.settleD == 0) return true;
            if (fabs(drivePID.derivative) < ec.settleD) return true;
        }
    }

    // perpendicular line cross check — if enabled
    if (ec.perpLine > 0) {
        double dx = targetX - chassis.x;
        double dy = targetY - chassis.y;
        double distToTarget = sqrt(dx * dx + dy * dy);

        // latch on once robot enters the activation radius
        if (distToTarget <= ec.perpLine) {
            exitStateVars.perpLineActive = true;
        }

        // once latched, exit when the target crosses the robot's perpendicular axis
        // project robot→target onto the robot's forward axis (clockwise-positive heading, 0 = north)
        // positive = target is ahead, negative = target is behind
        if (exitStateVars.perpLineActive) {
            double hRad = chassis.h * (M_PI / 180.0);
            double projection = dx * sin(hRad) + dy * cos(hRad);

            if (!backward && projection <= 0) {
                return true;
            }
            if (backward && projection >= 0) {
                return true;
            }
        }
    }

    return false;
}

bool checkTurnExit(TurnExitConditions& ec, ExitStateVars& exitStateVars,
                   double turnError) {

    double currentTime = Brain.timer(msec);

    // timeout check — if enabled
    if (ec.timeout > 0) {
        if (currentTime - exitStateVars.startTime >= ec.timeout) {
            return true;
        }
    }

    // exitAng + settle check — if enabled
    if (fabs(turnError) <= ec.exitAng) {
        if (ec.settleD == 0) return true;
        if (fabs(turnPID.derivative) < ec.settleD) return true;
    }

    return false;
}
