// moveTo.cpp — the lyapunov point movement: RAMSETE's law with the schedule
// taken away, so v_d comes from the distance to a fixed target instead of a row.
//
// Same plumbing as lyPose (drivePID + headingPID in volts, the law mixes its own
// left/right, governOutputs after) and the same two stages as classic moveTo.
// What changed from lyPose is the frame: the heading to the target is recomputed
// every tick rather than committed at the first loop.

#include "ramsete/util/ramseteController.h"
#include "movement/movementUtils.h"
#include "movement/odometry.h"
#include "movement/pid.h"
#include "coreConfigs.h"
#include "misc/util.h"
#include "debugger/debugger.h"

#include <cmath>

void Ramsete::moveTo(double x, double y, LateralExitConditions ec) {
    double startTime = Brain.timer(msec);

    clearAllValues();

    target.x = x;
    target.y = y;
    debugger::movePoint("ramsete.moveTo", x, y, ec, backward, false);

    MoveToError seed = moveToError(target.x, target.y,
                                   chassis.x, chassis.y, chassis.h, backward);
    headingError = seed.heading;
    lateralError = seed.distance;

    while (not exitConditionFulfilled) {
        MoveToError ramseteError = moveToError(target.x, target.y,
                                               chassis.x, chassis.y, chassis.h, backward);

        //if we're far from the target, use point based heading error and distance.
        if (not close) {
            headingError = ramseteError.heading;
            lateralError = ramseteError.distance + (backward ? -drivePID.targetOffset : drivePID.targetOffset);

            if (fabs(ramseteError.distance) < 0.5) {
                close = true;
                target.h = chassis.h;
                targetStraightDistance = lateralError;
                chassis.clearTrackedInches();
            }
        }
        
        // if we're close to the target, no more active point-based heading correction
        if (close) {
            headingError = chassis.wrappedHeadingError(target.h);
            lateralError = targetStraightDistance - chassis.yInchesTracked;
        }

        drivePow = cos(TO_RAD(headingError)) * drivePID.calculateOutput(lateralError);
        headingPow = headingPID.calculateOutput(headingError) + (not close? moveToTurn(ramseteError, drivePow) : 0);

        debugger::probeRamseteMove(ramseteError.crossTrack);

        //blend drive + turn into left/right voltages (cosine + turn-priority + blend)
        blendOutputs({.cosineScaling = false, .turnPriority = 0});

        //enforce output constraints (slew, ratio-preserving max cap, min floor)
        governOutputs();

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //check exit conditions
        exitConditionFulfilled = checkLateralExit(ec, startTime);

        delay(10);
    }
    drivePID.targetOffset = 0;
    voltageDrive(0, 0);
}
