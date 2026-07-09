#include "movement/exitConditions.h"
#include "coreConfigs.h"

#include <cmath>

void resetExitStateVars(ExitStateVars& exitStateVars) {
    exitStateVars.startTime      = Brain.timer(msec);
    exitStateVars.perpLineActive = false;
}

bool checkLateralExit(LateralExitConditions& ec, ExitStateVars& exitStateVars,
                      double driveError,
                      double targetX, double targetY,
                      bool backward) {

    double currentTime = Brain.timer(msec);

    // timeout check
    if (ec.tOut > 0) { //if enabled
        if (currentTime - exitStateVars.startTime >= ec.tOut) return true;
    }

    // exit distance + settle check
    if (ec.extD > 0) { //if enabled
        if (fabs(driveError) <= ec.extD) {
            if (ec.setD == 0) return true;
            if (fabs(drivePID.derivative) < ec.setD) return true;
        }
    }

    // perpendicular-line proximity check — if enabled
    // pLine is the robot's exit distance to the line through the target that runs
    // ⊥ to the robot's heading. Arms once the robot is within extD of the target
    // (so it can't fire early while off to the side), then exits when the robot's
    // perpendicular distance to that line drops to pLine.
    if (ec.pLine > 0) {
        double dx = targetX - chassis.x;
        double dy = targetY - chassis.y;
        double distToTarget = sqrt(dx * dx + dy * dy);

        // arm once robot enters the extD activation radius
        if (ec.extD > 0 && distToTarget <= ec.extD) {
            exitStateVars.perpLineActive = true;
        }

        // once armed, the robot's distance to the perpendicular line is the
        // magnitude of robot→target projected onto the heading axis
        // (clockwise-positive heading, 0 = north).
        if (exitStateVars.perpLineActive) {
            double hRad = chassis.h * (M_PI / 180.0);
            double projection = dx * sin(hRad) + dy * cos(hRad);

            if (fabs(projection) <= ec.pLine) return true;
        }
    }

    return false;
}

bool checkTurnExit(TurnExitConditions& ec, ExitStateVars& exitStateVars,
                   double turnError) {

    double currentTime = Brain.timer(msec);

    // timeout check — if enabled
    if (ec.tOut > 0) {
        if (currentTime - exitStateVars.startTime >= ec.tOut) {
            return true;
        }
    }

    // exit angle + settle check — if enabled
    if (fabs(turnError) <= ec.extA) {
        if (ec.setD == 0) return true;
        if (fabs(turnPID.derivative) < ec.setD) return true;
    }

    return false;
}
