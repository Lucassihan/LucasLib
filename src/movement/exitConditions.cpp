#include "movement/exitConditions.h"
#include "movementUtils.h"
#include "coreConfigs.h"

#include <cmath>

bool checkLateralExit(LateralExitConditions& ec, double startTime,
                      bool hasPointTarget) {

    double currentTime = Brain.timer(msec);
    double vNow = chassis.v;

    if (ec.t != -1 and currentTime - startTime >= ec.t) return true;

    if (ec.v != -1 and fabs(vNow) < fabs(lastV) and fabs(lateralError) < ec.settleGate and fabs(vNow) < ec.v) return true;

    //CROSS — skipped without a point target: drive() is relative, so targetX/targetY
    //are still at the clearAllValues() zero and the line would sit on the field origin.
    if (hasPointTarget and ec.d != -1 and crossed(targetX, targetY, ec.d)) return true;

    lastV = vNow;
    return false;
}

bool checkTurnExit(TurnExitConditions& ec, double startTime) {

    double currentTime = Brain.timer(msec);
    double ωNow = chassis.ω;

    if (ec.t != -1 and currentTime - startTime >= ec.t) return true;

    if (ec.v != -1 and fabs(ωNow) < fabs(lastω) and fabs(headingError) < ec.settleGate and fabs(ωNow) < ec.v) return true;

    if (ec.d != -1 and fabs(headingError) <= ec.d) return true;

    lastω = ωNow;
    return false;
}
