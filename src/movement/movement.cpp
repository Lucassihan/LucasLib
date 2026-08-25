#include "movement/movement.h"
#include "movement/totalOutput.h"
#include "movement/pid.h"
#include "coreConfigs.h"
#include "misc/util.h"
#include "movement/exitConditions.h"
#include "debugger/debugger.h"
#include <cmath>
#include <algorithm>

////////////////////////////////////////////
//   _______ _    _ _____  _   _  _____ 
//  |__   __| |  | |  __ \| \ | |/ ____|
//     | |  | |  | | |__) |  \| | (___  
//     | |  | |  | |  _  /| . ` |\___ \ 
//     | |  | |__| | | \ \| |\  |____) |
//     |_|   \____/|_|  \_\_| \_|_____/ 
////////////////////////////////////////////                              

void face(double heading, TurnExitConditions ec, int direction) {
    double startTime = Brain.timer(msec);
    clearAllValues();
    debugger::moveTurn("face", heading, ec);

    int offsetDir = (direction == 0) ? sign(chassis.wrappedHeadingError(heading)) : direction;

    while (not exitConditionFulfilled) {
        //calculate heading error
        headingError = resolveHeadingTarget(heading + turnPID.targetOffset * offsetDir, direction);

        //calculate turn power based on the heading error calculated above using PID
        turnPow = turnPID.calculateOutput(headingError);

        //distribute turn power to the left and right sides of the drive
        leftOut = turnPow;
        rightOut = -turnPow;

        //make sure leftOut and rightOut follow minOutput, maxOutput, minAccelRate, and maxAccelRate from totalOutput
        governOutputs();

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //check exit conditions
        exitConditionFulfilled = checkTurnExit(ec, startTime);

        delay(10);
    }
    turnPID.targetOffset = 0;
    voltageDrive(0,0); //stop the motors when the movement ends
}

void facePoint(double x, double y, TurnExitConditions ec, int direction) {
    double startTime = Brain.timer(msec);
    clearAllValues();
    target.x = x;
    target.y = y;
    debugger::movePointTurn("facePoint", x, y, ec);

    int offsetDir = (direction == 0) ? sign(chassis.wrappedPointHeadingError(target.x, target.y, backward)) : direction;

    while (not exitConditionFulfilled) {
        //calculate heading error
        headingError = resolveHeadingTarget(chassis.angleToPoint(target.x, target.y, backward) + turnPID.targetOffset * offsetDir, direction);

        //calculate turn power based on the heading error calculated above using PID
        turnPow = turnPID.calculateOutput(headingError);

        //distribute turn power to the left and right sides of the drive
        leftOut = turnPow;
        rightOut = -turnPow;

        //make sure leftOut and rightOut follow minOutput, maxOutput, minAccelRate, and maxAccelRate from totalOutput
        governOutputs();

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //check exit conditions
        exitConditionFulfilled = checkTurnExit(ec, startTime);

        delay(10);
    }
    turnPID.targetOffset = 0;
    voltageDrive(0,0); //stop the motors when the movement ends
}

void facePoint(BoomParams bp, TurnExitConditions ec, int direction) {
    facePoint(getBoomLeadX(bp, backward), getBoomLeadY(bp, backward), ec, direction);
}

void swingOnLeft(double heading, TurnExitConditions ec, double partiality, int direction) {
    double startTime = Brain.timer(msec);
    clearAllValues();
    debugger::moveTurn("swingOnLeft", heading, ec, partiality);

    int offsetDir = (direction == 0) ? sign(chassis.wrappedHeadingError(heading)) : direction;

    while (not exitConditionFulfilled) {
        //calculate heading error
        headingError = resolveHeadingTarget(heading + turnPID.targetOffset * offsetDir, direction);

        //calculate swing power based on the heading error calculated above using PID
        if (partiality < 0) {
            swingPow = turnPID.calculateOutput(headingError);
        } else {
            swingPow = swingPID.calculateOutput(headingError);
        }

        //distribute swing power to the left and right sides of the drive
        leftOut = -swingPow * partiality;
        rightOut = -swingPow;

        //apply to the motors
        voltageDrive(leftOut, rightOut, true);

        //check exit conditions
        exitConditionFulfilled = checkTurnExit(ec, startTime);

        delay(10);
    }
    turnPID.targetOffset = 0;
    voltageDrive(0,0); //stop the motors when the movement ends
}

void swingOnRight(double heading, TurnExitConditions ec, double partiality, int direction) {
    double startTime = Brain.timer(msec);
    clearAllValues();
    debugger::moveTurn("swingOnRight", heading, ec, partiality);

    int offsetDir = (direction == 0) ? sign(chassis.wrappedHeadingError(heading)) : direction;

    while (not exitConditionFulfilled) {
        //calculate heading error
        headingError = resolveHeadingTarget(heading + turnPID.targetOffset * offsetDir, direction);

        //calculate swing power based on the heading error calculated above using PID
        if (partiality < 0) {
            swingPow = turnPID.calculateOutput(headingError);
        } else {
            swingPow = swingPID.calculateOutput(headingError);
        }

        //distribute swing power to the left and right sides of the drive
        leftOut = swingPow;
        rightOut = swingPow * partiality;

        //apply to the motors
        voltageDrive(leftOut, rightOut, true);

        //check exit conditions
        exitConditionFulfilled = checkTurnExit(ec, startTime);

        delay(10);
    }
    turnPID.targetOffset = 0;
    voltageDrive(0,0); //stop the motors when the movement ends
}

void swingOnLeftToPoint(double x, double y, TurnExitConditions ec, double partiality, int direction) {
    double startTime = Brain.timer(msec);
    clearAllValues();
    target.x = x;
    target.y = y;
    debugger::movePointTurn("swingOnLeftToPoint", x, y, ec, partiality);

    int offsetDir = (direction == 0) ? sign(chassis.wrappedPointHeadingError(target.x, target.y, backward)) : direction;

    while (not exitConditionFulfilled) {
        //calculate heading error
        headingError = resolveHeadingTarget(chassis.angleToPoint(target.x, target.y, backward) + turnPID.targetOffset * offsetDir, direction);

        //calculate swing power based on the heading error calculated above using PID
        if (partiality < 0) {
            swingPow = turnPID.calculateOutput(headingError);
        } else {
            swingPow = swingPID.calculateOutput(headingError);
        }

        //distribute swing power to the left and right sides of the drive
        leftOut = -swingPow * partiality;
        rightOut = -swingPow;

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //check exit conditions
        exitConditionFulfilled = checkTurnExit(ec, startTime);

        delay(10);
    }
    turnPID.targetOffset = 0;
    voltageDrive(0,0); //stop the motors when the movement ends
}

void swingOnRightToPoint(double x, double y, TurnExitConditions ec, double partiality, int direction) {
    double startTime = Brain.timer(msec);
    clearAllValues();
    target.x = x;
    target.y = y;
    debugger::movePointTurn("swingOnRightToPoint", x, y, ec, partiality);

    int offsetDir = (direction == 0) ? sign(chassis.wrappedPointHeadingError(target.x, target.y, backward)) : direction;

    while (not exitConditionFulfilled) {
        //calculate heading error
        headingError = resolveHeadingTarget(chassis.angleToPoint(target.x, target.y, backward) + turnPID.targetOffset * offsetDir, direction);

        //calculate swing power based on the heading error calculated above using PID
        if (partiality < 0) {
            swingPow = turnPID.calculateOutput(headingError);
        } else {
            swingPow = swingPID.calculateOutput(headingError);
        }

        //distribute swing power to the left and right sides of the drive
        leftOut = swingPow;
        rightOut = swingPow * partiality;

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //check exit conditions
        exitConditionFulfilled = checkTurnExit(ec, startTime);

        delay(10);
    }
    turnPID.targetOffset = 0;
    voltageDrive(0,0); //stop the motors when the movement ends
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//   _            _______ ______ _____            _        __  __  ______      ________ __  __ ______ _   _ _______ _____ 
//  | |        /\|__   __|  ____|  __ \     /\   | |      |  \/  |/ __ \ \    / /  ____|  \/  |  ____| \ | |__   __/ ____|
//  | |       /  \  | |  | |__  | |__) |   /  \  | |      | \  / | |  | \ \  / /| |__  | \  / | |__  |  \| |  | | | (___  
//  | |      / /\ \ | |  |  __| |  _  /   / /\ \ | |      | |\/| | |  | |\ \/ / |  __| | |\/| |  __| | . ` |  | |  \___ \ 
//  | |____ / ____ \| |  | |____| | \ \  / ____ \| |____  | |  | | |__| | \  /  | |____| |  | | |____| |\  |  | |  ____) |
//  |______/_/    \_\_|  |______|_|  \_\/_/    \_\______| |_|  |_|\____/   \/   |______|_|  |_|______|_| \_|  |_| |_____/ 
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////        

void driveForward(double distance, LateralExitConditions ec, double heading, BlendSettings bs) {
    double startTime = Brain.timer(msec);
    clearAllValues();
    target.h = heading;
    debugger::moveStraight("driveForward", distance, heading, ec, false);

    while (not exitConditionFulfilled) {
        //calculate lateral error
        lateralError = distance - chassis.yInchesTracked + drivePID.targetOffset;

        //calculate heading error
        headingError = chassis.wrappedHeadingError(target.h);

        //calculate drive and turn power based on the errors calculated above using PID
        drivePow   = drivePID.calculateOutput(lateralError);
        headingPow = headingPID.calculateOutput(headingError);

        //distribute drive and turn power to the left and right sides of the drive
        blendOutputs(bs);

        //make sure leftOut and rightOut follow minOutput, maxOutput, minAccelRate, and maxAccelRate from totalOutput
        governOutputs();

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //check exit conditions
        exitConditionFulfilled = checkLateralExit(ec, startTime, false)
                                 or fabs(lateralError) < ec.d;

        delay(10);
    }
    drivePID.targetOffset = 0;
    voltageDrive(0,0); //stop the motors when the movement ends
}

void driveBackward(double distance, LateralExitConditions ec, double heading, BlendSettings bs) {
    double startTime = Brain.timer(msec);
    clearAllValues();
    target.h = heading;
    debugger::moveStraight("driveBackward", distance, heading, ec, true);

    while (not exitConditionFulfilled) {
        //calculate lateral error
        lateralError = -(distance + chassis.yInchesTracked)- drivePID.targetOffset;

        //calculate heading error
        headingError = chassis.wrappedHeadingError(target.h);

        //calculate drive and turn power based on the errors calculated above using PID
        drivePow   = drivePID.calculateOutput(lateralError);
        headingPow = headingPID.calculateOutput(headingError);

        //distribute drive and turn power to the left and right sides of the drive
        blendOutputs(bs);

        //make sure leftOut and rightOut follow minOutput, maxOutput, minAccelRate, and maxAccelRate from totalOutput
        governOutputs();

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //check exit conditions
        exitConditionFulfilled = checkLateralExit(ec, startTime, false)
                                 or fabs(lateralError) < ec.d;

        delay(10);
    }
    drivePID.targetOffset = 0;
    voltageDrive(0,0); //stop the motors when the movement ends
}

double turnDisableLength = 6;        // moveTo: within this distance, stop point-aiming and hold a fixed heading
void moveTo(double x, double y, LateralExitConditions ec, BlendSettings bs) {
    double startTime = Brain.timer(msec);
    clearAllValues();
    target.x = x;
    target.y = y;
    debugger::movePoint("moveTo", x, y, ec, backward);

    headingError = chassis.wrappedPointHeadingError(target.x, target.y, backward);
    lateralError = chassis.distanceTo(target.x, target.y, backward);

    while (not exitConditionFulfilled) {
        //calculate lateral and heading error: while far from the target, both are point based
        if (not close) {
            headingError = chassis.wrappedPointHeadingError(target.x, target.y, backward);
            lateralError = chassis.distanceTo(target.x, target.y, backward) + (backward ? -drivePID.targetOffset : drivePID.targetOffset);

            if (fabs(chassis.distanceTo(target.x, target.y, backward)) < turnDisableLength) {
                close = true;
                target.h = chassis.h;
                targetStraightDistance = lateralError;
                chassis.clearTrackedInches();
            }
        }

        //once close, both come off the latched heading and straight-line distance instead
        if (close) {
            headingError = chassis.wrappedHeadingError(target.h);
            lateralError = targetStraightDistance - chassis.yInchesTracked;
        }

        //calculate drive and turn power based on the errors calculated above using PID
        drivePow   = drivePID.calculateOutput(lateralError);
        headingPow = headingPID.calculateOutput(headingError);

        //distribute drive and turn power to the left and right sides of the drive
        blendOutputs(bs);

        //make sure leftOut and rightOut follow minOutput, maxOutput, minAccelRate, and maxAccelRate from totalOutput
        governOutputs();

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //check exit conditions
        exitConditionFulfilled = checkLateralExit(ec, startTime);

        delay(10);
    }
    drivePID.targetOffset = 0;
    voltageDrive(0,0); //stop the motors when the movement ends
}

// Three-stage "ghost point" boomerang (ported from 2029C Inception's movePose),
// with the lead reparameterized: the carrot offset is a COEFFICIENT of the initial
// robot->target distance (bp.lead), not raw inches. Stages, all driven by classic
// PID + cosine via blendOutputs/governOutputs:
//   FAR  - steer at the GHOST (a point trailing behind the carrot by bp.gLead)
//   MID  - steer at the CARROT (the retracting lead point)
//   END  - drive to the target, snapping to the absolute heading once on top of it
// Steering tracks the stage point; SPEED always tracks the true target distance.
// The carrot retracts via Daniel's monotonic-min formula, which (unlike a live
// distance factor) never springs back out if chasing the lead briefly increases
// the robot's distance to the target.
void boomerangTo(BoomParams bp,
                 LateralExitConditions ec) {
    double x = bp.x;
    double y = bp.y;
    double heading = bp.heading;
    double startTime = Brain.timer(msec);
    clearAllValues();
    target.x = x;
    target.y = y;
    target.h = heading;
    debugger::moveBoomerang(bp, ec);

    double headingRad = TO_RAD(heading);

    // Flip lead side for backward so the robot backs in and the HEAD still ends up
    // facing `heading` (absolute heading convention).
    double leadSign = -1.0;
    if (backward) {
        leadSign = 1.0;
    }
    double dirX = leadSign * sin(headingRad);
    double dirY = leadSign * cos(headingRad);

    // Initial (un-retracted) carrot, planted bp.lead * initial-distance behind the target.
    double initialLeadDist = bp.lead * fabs(chassis.distanceTo(x, y));
    double initCarrotX = x + dirX * initialLeadDist;
    double initCarrotY = y + dirY * initialLeadDist;

    double carrotX = initCarrotX;
    double carrotY = initCarrotY;

    double initCarrotDist = hypot(carrotX - chassis.x, carrotY - chassis.y);
    double minCarrotDist  = initCarrotDist;

    // Sticky stage latches: FAR -> MID -> END, one-way.
    bool closeGhost = false;
    bool closeEnd   = false;

    // Speed-scaled ghost->carrot switch distance (Daniel's 2*maxSpeed, with maxSpeed
    // recovered as a fraction of the 12 V full scale). Invariant across the move.
    double ghostSwitchDist = 6.0 * (takeSmallerBetween(drivePID.maxOutput, totalOutput.maxOutput) / 12.0);

    while (not exitConditionFulfilled) {
        // --- Retract the carrot (monotonic-min of the robot->carrot distance) ---
        double curCarrotDist = hypot(carrotX - chassis.x, carrotY - chassis.y);
        if (curCarrotDist < minCarrotDist) {
            minCarrotDist = curCarrotDist;
        }
        double s = 0;
        if (initCarrotDist > 0) {
            s = minCarrotDist / initCarrotDist;
        }
        carrotX = x + dirX * s * initialLeadDist;
        carrotY = y + dirY * s * initialLeadDist;

        // --- Ghost trails behind the carrot toward the original carrot by bp.gLead ---
        double ghostX = initCarrotX + (carrotX - initCarrotX) * (1 - bp.gLead);
        double ghostY = initCarrotY + (carrotY - initCarrotY) * (1 - bp.gLead);

        // --- Distances used for staging (unsigned) ---
        double distEnd    = hypot(x - chassis.x, y - chassis.y);
        double distCarrot = hypot(carrotX - chassis.x, carrotY - chassis.y);
        double distGhost  = hypot(ghostX  - chassis.x, ghostY  - chassis.y);

        // --- Stage selection: STEERING tracks the stage point ---
        if (distCarrot < 0.7 or closeEnd) {
            closeEnd = true;
            if (distEnd < 0.7) {
                headingError = chassis.wrappedHeadingError(heading);
            } else {
                headingError = chassis.wrappedPointHeadingError(x, y, backward);
            }
        } else if (distGhost < ghostSwitchDist or closeGhost) {
            closeGhost = true;
            headingError = chassis.wrappedPointHeadingError(carrotX, carrotY, backward);
        } else {
            headingError = chassis.wrappedPointHeadingError(ghostX, ghostY, backward);
        }

        // --- SPEED tracks the true target distance (carrot only when it is the
        // nearer waypoint and we haven't latched the end) ---
        if (closeEnd or distEnd > distCarrot) {
            lateralError = chassis.distanceTo(x, y, backward);
        } else {
            lateralError = chassis.distanceTo(carrotX, carrotY, backward);
        }

        //calculate drive power based on the lateral error calculated above using PID
        drivePow = drivePID.calculateOutput(lateralError);

        // --- Centripetal slip cap: v_max = sqrt(grip * |r|). Skip right at the end. ---
        double maxSlip = 0;  // 0 = cap inactive (used for debugger telemetry)
        if (bp.leadChasePowerLimit > 0 and (not closeEnd or distEnd >= 0.7)) {
            maxSlip = sqrt(bp.leadChasePowerLimit * fabs(chassis.travelArcRadius()));
            if (fabs(drivePow) > maxSlip) {
                drivePow = sign(drivePow) * maxSlip;
            }
        }

        // --- Settle overshoot brake (Daniel's settle cosine) ---
        // In the final inch, steering switches to the absolute heading, so
        // blendOutputs' cosine no longer senses passing the target, and the
        // fixed-sign distance magnitude would otherwise run the robot away on
        // overshoot. Re-introduce the bearing-to-target cosine: it flips drive
        // once the target falls behind, pulling the robot back to settle.
        if (closeEnd and distEnd < 0.7) {
            drivePow *= cos(TO_RAD(chassis.wrappedPointHeadingError(x, y, backward)));
        }

        //calculate turn power based on the heading error calculated above using PID
        headingPow = headingPID.calculateOutput(headingError);

        //distribute drive and turn power to the left and right sides of the drive
        blendOutputs({.cosineScaling = false, .turnPriority = 0});

        //make sure leftOut and rightOut follow minOutput, maxOutput, minAccelRate, and maxAccelRate from totalOutput
        governOutputs();

        debugger::probeBoomerang(carrotX, carrotY, ghostX, ghostY, s, maxSlip,
                                 closeEnd ? 2 : (closeGhost ? 1 : 0));

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //check exit conditions
        exitConditionFulfilled = checkLateralExit(ec, startTime);

        delay(10);
    }
    voltageDrive(0,0); //stop the motors when the movement ends
}

void voldrive(double left, double right) {
    debugger::moveVoltage(left, right);
    voltageDrive(left,right);
}

void voldrive(double volts) {
    debugger::moveVoltage(volts, volts);
    voltageDrive(volts, volts);
}