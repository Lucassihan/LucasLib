#include "movement/exitConditions.h"
#include "coreConfigs.h"

#include <cmath>

bool checkLateralExit(LateralExitConditions& ec, double startTime,
                      double driveError,
                      double targetX, double targetY,
                      bool backward) {

    double currentTime = Brain.timer(msec);

    if (ec.tOut > 0) {
        if (currentTime - startTime >= ec.tOut) return true;
    }

    if (ec.setR > 0) {
        if (fabs(driveError) <= ec.setR and fabs(drivePID.derivative) <= ec.setD or ec.setD == -1) return true;
    }

    // Which SIDE of the line are we on — not how close to it. A proximity test needs
    // width to fire, so crossD = 0 could only ever work by faking one, and any width
    // small enough to be accurate is small enough to skip over at speed (a 10 ms tick
    // at 60 in/s moves 0.6 in). A side test can't be skipped: along starts positive
    // with the line ahead, ends negative once past, and never gets there without
    // passing through. crossD just slides the line toward us to fire that much early.
    if (ec.crossD >= 0) {
        double dx = targetX - chassis.x;
        double dy = targetY - chassis.y;
        double hRad = chassis.h * (M_PI / 180.0);
        double along = dx * sin(hRad) + dy * cos(hRad); // + = line ahead, - = passed it
        if (backward) along = -along;                   // measure along the way we travel
        if (along <= ec.crossD) return true;
    }

    return false;
}

bool checkTurnExit(TurnExitConditions& ec, double startTime,
                   double turnError) {

    double currentTime = Brain.timer(msec);

    if (ec.tOut > 0) {
        if (currentTime - startTime >= ec.tOut) return true;
    }

    // SETTLE — same shape as the lateral channel: inside the activation zone AND
    // the error has stopped moving. setR is the only on/off switch; setD is a
    // threshold, so setD = 0 makes settle unreachable rather than instant.
    if (ec.setR > 0) {
        if (fabs(turnError) <= ec.setR and fabs(turnPID.derivative) < ec.setD) return true;
    }

    // ANGLE — independent of settle, and deliberately ungated: this is the "close
    // enough, don't wait for it to stop" exit, so it can fire mid-rotation.
    if (ec.extA > 0) {
        if (fabs(turnError) <= ec.extA) return true;
    }

    return false;
}
