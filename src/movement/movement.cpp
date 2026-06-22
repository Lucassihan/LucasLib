#include "movement/movement.h"
#include "movement/totalOutput.h"
#include "movement/pid.h"
#include "coreConfigs.h"
#include "misc/util.h"
#include "movement/exitConditions.h"
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
    ExitStateVars exitStateVars;
    clearAllValues();

    //convert exitPct to exitAng using the initial heading error
    if (ec.exitPct > 0) {
        double initialError = resolveHeadingTarget(heading, false, direction);
        double pctExitAng = (1.0 - ec.exitPct) * fabs(initialError);
        if (pctExitAng > ec.exitAng) {
            ec.exitAng = pctExitAng;
        }
    }

    while (not exitConditionFulfilled) {
        //heading error determination
        headingError = resolveHeadingTarget(heading, false, direction);

        //calculate turn power based on the heading error calculated above
        turnPow = turnPID.calculateOutput(headingError + turnOvershoot);

        //distribute turn power to the left and right sides of the drive
        leftOut = turnPow;
        rightOut = -turnPow;

        //make sure leftOut and rightOut follow minOutput, maxOutput, minAccelRate, and maxAccelRate constraints based on the settings in totalOutput
        governOutputs();

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //check exit conditions
        exitConditionFulfilled = checkTurnExit(ec, exitStateVars, headingError);

        delay(10);
    }
    turnOvershoot = 0;
    voltageDrive(0,0); //Stop the motors when the movement ends
}

void facePoint(double x, double y, TurnExitConditions ec, bool backward, int direction) {
    ExitStateVars exitStateVars;
    clearAllValues();
    targetX = x;
    targetY = y;

    //convert exitPct to exitAng using the initial heading error
    if (ec.exitPct > 0) {
        double initialError = resolvePointTarget(targetX, targetY, backward, direction);
        double pctExitAng = (1.0 - ec.exitPct) * fabs(initialError);
        if (pctExitAng > ec.exitAng) {
            ec.exitAng = pctExitAng;
        }
    }

    while (not exitConditionFulfilled) {
        //heading error determination
        headingError = resolvePointTarget(targetX, targetY, backward, direction);

        //calculate turn power based on the heading error calculated above
        turnPow = turnPID.calculateOutput(headingError + turnOvershoot);

        //distribute turn power to the left and right sides of the drive
        leftOut = turnPow;
        rightOut = -turnPow;

        //make sure leftOut and rightOut follow minOutput, maxOutput, minAccelRate, and maxAccelRate constraints based on the settings in totalOutput
        governOutputs();

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //check exit conditions
        exitConditionFulfilled = checkTurnExit(ec, exitStateVars, headingError);

        delay(10);
    }
    turnOvershoot = 0;
    voltageDrive(0,0); //Stop the motors when the movement ends
}

void swingOnLeft(double heading, TurnExitConditions ec, double partiality, int direction) {
    ExitStateVars exitStateVars;
    clearAllValues();

    //convert exitPct to exitAng using the initial heading error
    if (ec.exitPct > 0) {
        double initialError = resolveHeadingTarget(heading, false, direction);
        double pctExitAng = (1.0 - ec.exitPct) * fabs(initialError);
        if (pctExitAng > ec.exitAng) {
            ec.exitAng = pctExitAng;
        }
    }

    while (not exitConditionFulfilled) {
        //heading error determination
        headingError = resolveHeadingTarget(heading, false, direction);

        if (partiality < 0) {
            swingPow = turnPID.calculateOutput(headingError + turnOvershoot);
        } else {
            swingPow = swingPID.calculateOutput(headingError + turnOvershoot);
        }

        leftOut = -swingPow * partiality;
        rightOut = -swingPow;

        voltageDrive(leftOut, rightOut, true);

        exitConditionFulfilled = checkTurnExit(ec, exitStateVars, headingError);

        delay(10);
    }
    turnOvershoot = 0;
    voltageDrive(0,0); //Stop the motors when the movement ends
}

void swingOnRight(double heading, TurnExitConditions ec, double partiality, int direction) {
    ExitStateVars exitStateVars;
    clearAllValues();

    //convert exitPct to exitAng using the initial heading error
    if (ec.exitPct > 0) {
        double initialError = resolveHeadingTarget(heading, false, direction);
        double pctExitAng = (1.0 - ec.exitPct) * fabs(initialError);
        if (pctExitAng > ec.exitAng) {
            ec.exitAng = pctExitAng;
        }
    }

    while (not exitConditionFulfilled) {
        //heading error determination
        headingError = resolveHeadingTarget(heading, false, direction);

        if (partiality < 0) {
            swingPow = turnPID.calculateOutput(headingError + turnOvershoot);
        } else {
            swingPow = swingPID.calculateOutput(headingError + turnOvershoot);
        }

        leftOut = swingPow;
        rightOut = swingPow * partiality;

        voltageDrive(leftOut, rightOut, true);

        exitConditionFulfilled = checkTurnExit(ec, exitStateVars, headingError);

        delay(10);
    }
    turnOvershoot = 0;
    voltageDrive(0,0); //Stop the motors when the movement ends
}

void swingOnLeftToPoint(double x, double y, TurnExitConditions ec, bool backward, double partiality, int direction) {
    ExitStateVars exitStateVars;
    clearAllValues();
    targetX = x;
    targetY = y;

    //convert exitPct to exitAng using the initial heading error
    if (ec.exitPct > 0) {
        double initialError = resolvePointTarget(targetX, targetY, backward, direction);
        double pctExitAng = (1.0 - ec.exitPct) * fabs(initialError);
        if (pctExitAng > ec.exitAng) {
            ec.exitAng = pctExitAng;
        }
    }

    while (not exitConditionFulfilled) {
        //heading error determination
        headingError = resolvePointTarget(targetX, targetY, backward, direction);

        if (partiality < 0) {
            swingPow = turnPID.calculateOutput(headingError + turnOvershoot);
        } else {
            swingPow = swingPID.calculateOutput(headingError + turnOvershoot);
        }

        leftOut = -swingPow * partiality;
        rightOut = -swingPow;

        voltageDrive(leftOut, rightOut);

        exitConditionFulfilled = checkTurnExit(ec, exitStateVars, headingError);

        delay(10);
    }
    turnOvershoot = 0;
    voltageDrive(0,0); //Stop the motors when the movement ends
}

void swingOnRightToPoint(double x, double y, TurnExitConditions ec, bool backward, double partiality, int direction) {
    ExitStateVars exitStateVars;
    clearAllValues();
    targetX = x;
    targetY = y;

    //convert exitPct to exitAng using the initial heading error
    if (ec.exitPct > 0) {
        double initialError = resolvePointTarget(targetX, targetY, backward, direction);
        double pctExitAng = (1.0 - ec.exitPct) * fabs(initialError);
        if (pctExitAng > ec.exitAng) {
            ec.exitAng = pctExitAng;
        }
    }

    while (not exitConditionFulfilled) {
        //heading error determination
        headingError = resolvePointTarget(targetX, targetY, backward, direction);

        if (partiality < 0) {
            swingPow = turnPID.calculateOutput(headingError + turnOvershoot);
        } else {
            swingPow = swingPID.calculateOutput(headingError + turnOvershoot);
        }

        leftOut = swingPow;
        rightOut = swingPow * partiality;

        voltageDrive(leftOut, rightOut);

        exitConditionFulfilled = checkTurnExit(ec, exitStateVars, headingError);

        delay(10);
    }
    turnOvershoot = 0;
    voltageDrive(0,0); //Stop the motors when the movement ends
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//   _            _______ ______ _____            _        __  __  ______      ________ __  __ ______ _   _ _______ _____ 
//  | |        /\|__   __|  ____|  __ \     /\   | |      |  \/  |/ __ \ \    / /  ____|  \/  |  ____| \ | |__   __/ ____|
//  | |       /  \  | |  | |__  | |__) |   /  \  | |      | \  / | |  | \ \  / /| |__  | \  / | |__  |  \| |  | | | (___  
//  | |      / /\ \ | |  |  __| |  _  /   / /\ \ | |      | |\/| | |  | |\ \/ / |  __| | |\/| |  __| | . ` |  | |  \___ \ 
//  | |____ / ____ \| |  | |____| | \ \  / ____ \| |____  | |  | | |__| | \  /  | |____| |  | | |____| |\  |  | |  ____) |
//  |______/_/    \_\_|  |______|_|  \_\/_/    \_\______| |_|  |_|\____/   \/   |______|_|  |_|______|_| \_|  |_| |_____/ 
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////        

void driveForward(double distance, LateralExitConditions ec, BlendSettings bs, double heading) {
    ExitStateVars exitStateVars;
    clearAllValues();
    targetH = heading;

    //Capture the pose at the start of the move. Error is the gap between the
    //commanded distance and how far we've actually moved along the heading axis
    //since then — read straight off (MCL-corrected) pose, not wheel encoders.
    double startX = chassis.x;
    double startY = chassis.y;

    while (not exitConditionFulfilled) {
        lateralError = distance - chassis.forwardProgress(startX, startY, targetH);
        lateralError = lateralError - lateralUndershoot;

        //hold the supplied heading (defaults to whatever chassis.h was at call time)
        headingError = chassis.wrappedHeadingError(targetH, false);

        drivePow   = drivePID.calculateOutput(lateralError);
        headingPow = headingPID.calculateOutput(headingError);

        //blend drive + turn into left/right voltages (cosine + turn-priority + blend)
        blendOutputs(bs);

        //enforce output constraints (slew, ratio-preserving max cap, min floor)
        governOutputs();

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //check exit conditions
        exitConditionFulfilled = checkLateralExit(ec, exitStateVars, lateralError, 0, 0);

        delay(10);
    }
    lateralUndershoot = 0;
    voltageDrive(0,0); //Stop the motors when the movement ends
}

void driveBackward(double distance, LateralExitConditions ec, BlendSettings bs, double heading) {
    ExitStateVars exitStateVars;
    clearAllValues();
    targetH = heading;

    //Same pose-based error as driveForward, mirrored: the target is -distance
    //along the heading axis, so backing up drives forwardProgress negative.
    double startX = chassis.x;
    double startY = chassis.y;

    while (not exitConditionFulfilled) {
        lateralError = -(distance + chassis.forwardProgress(startX, startY, targetH));
        lateralError = lateralError + lateralUndershoot;

        //hold the supplied heading (defaults to whatever chassis.h was at call time)
        headingError = chassis.wrappedHeadingError(targetH, false);

        drivePow   = drivePID.calculateOutput(lateralError);
        headingPow = headingPID.calculateOutput(headingError);

        //blend drive + turn into left/right voltages (cosine + turn-priority + blend)
        blendOutputs(bs);

        //enforce output constraints (slew, ratio-preserving max cap, min floor)
        governOutputs();

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //check exit conditions
        exitConditionFulfilled = checkLateralExit(ec, exitStateVars, lateralError, 0, 0, true);

        delay(10);
    }
    lateralUndershoot = 0;
    voltageDrive(0,0); //Stop the motors when the movement ends
}

double turnDisableLength = 6;        // moveTo: within this distance, stop point-aiming and hold a fixed heading
double lyPoseTurnDisableLength = 3;  // lyPose: within this distance, kill steering and drive straight in
void moveTo(double x, double y, LateralExitConditions ec, bool backward, BlendSettings bs) {
    ExitStateVars exitStateVars;
    clearAllValues();
    targetX = x;
    targetY = y;

    headingError = chassis.wrappedPointHeadingError(targetX, targetY, backward);
    lateralError = chassis.distanceTo(targetX, targetY, backward);

    while (not exitConditionFulfilled) {
        //if we're far from the target, use point based heading error and distance.
        if (not close) {
            headingError = chassis.wrappedPointHeadingError(targetX, targetY, backward);
            lateralError = chassis.distanceTo(targetX, targetY, backward);

            //check if we're close or not
            if (fabs(lateralError) < turnDisableLength) {
                close = true;
                targetH = chassis.h;
                targetStraightDistance = lateralError;
                chassis.clearTrackedInches();
            }
        }

        // If we're close to the target, no more active point-based heading correction
        if (close) {
            headingError = chassis.wrappedHeadingError(targetH, false);
            lateralError = targetStraightDistance - chassis.yInchesTracked;
        }

        drivePow   = drivePID.calculateOutput(lateralError - lateralUndershoot);
        headingPow = headingPID.calculateOutput(headingError);

        //blend drive + turn into left/right voltages (cosine + turn-priority + blend)
        blendOutputs(bs);

        //enforce output constraints (slew, ratio-preserving max cap, min floor)
        governOutputs();

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //check exit conditions
        exitConditionFulfilled = checkLateralExit(ec, exitStateVars, lateralError - lateralUndershoot, targetX, targetY, backward);

        delay(10);
    }
    lateralUndershoot = 0;
    voltageDrive(0,0); //Stop the motors when the movement ends
}

// Path-follow Lyapunov drive to a pose. Always rides the FIXED line through
// (x, y): with an explicit heading the line points that way; with heading = NAN
// the bot→target heading is frozen at the first loop (straightline-style steady
// heading) and held for the whole movement. Gives a consistent final heading at
// the target. Drives on e_x, so it backs up cleanly if it overshoots.
void lyPose(double x, double y, LateralExitConditions ec, bool backward,
            double heading) {
    ExitStateVars exitStateVars;
    clearAllValues();
    targetX = x;
    targetY = y;

    // Resolve the path heading ONCE and hold it. NAN → freeze the bot→target
    // heading from the start pose (same convention as wrappedPointHeadingError:
    // atan2(dx, dy), 0 = +Y, CW+).
    double pathHeading = heading;
    if (std::isnan(pathHeading)) {
        pathHeading = atan2(targetX - chassis.x, targetY - chassis.y) * (180.0 / M_PI);
    }

    // Run drivePID/headingPID raw — governor is the single rate-limiter.
    drivePID.executingLyapunov   = true;
    headingPID.executingLyapunov = true;

    headingError = chassis.wrappedHeadingError(pathHeading, backward);
    lateralError = chassis.distanceTo(targetX, targetY, backward);

    while (not exitConditionFulfilled) {
        //always path-follow; lyapunovCalculate writes leftOut/rightOut + lateralError
        lyapunovCalculate(targetX, targetY, backward, pathHeading);

        //CLOSE stage: within lyPoseTurnDisableLength of the pose, kill the turn
        //and drive straight in on the held heading (no last-inch heading jitter).
        if (lateralError < lyPoseTurnDisableLength) {
            leftOut    = driveOut;
            rightOut   = driveOut;
            turnOut    = 0;
            headingPow = 0;
        }

        //enforce output constraints (governor slew, ratio-preserving max cap, min floor)
        governOutputs();

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //exit settles on the TRUE distance to the pose (lateralError = dist)
        exitConditionFulfilled = checkLateralExit(ec, exitStateVars, lateralError - lateralUndershoot, targetX, targetY, backward);

        delay(10);
    }

    drivePID.executingLyapunov   = false;
    headingPID.executingLyapunov = false;
    lateralUndershoot = 0;
    voltageDrive(0,0); //Stop the motors when the movement ends
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
                 LateralExitConditions ec, bool backward) {
    double x = bp.x;
    double y = bp.y;
    double heading = bp.heading;
    ExitStateVars exitStateVars;
    clearAllValues();
    targetX = x;
    targetY = y;
    targetH = heading;

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
    double ghostSwitchDist = 2.0 * (totalOutput.maxOutput / 12.0);

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
        if (distCarrot < 0.7 || closeEnd) {
            closeEnd = true;
            if (distEnd < 0.7) {
                headingError = chassis.wrappedHeadingError(heading, false);
            } else {
                headingError = chassis.wrappedPointHeadingError(x, y, backward);
            }
        } else if (distGhost < ghostSwitchDist || closeGhost) {
            closeGhost = true;
            headingError = chassis.wrappedPointHeadingError(carrotX, carrotY, backward);
        } else {
            headingError = chassis.wrappedPointHeadingError(ghostX, ghostY, backward);
        }

        // --- SPEED tracks the true target distance (carrot only when it is the
        // nearer waypoint and we haven't latched the end) ---
        if (closeEnd || distEnd > distCarrot) {
            lateralError = chassis.distanceTo(x, y, backward);
        } else {
            lateralError = chassis.distanceTo(carrotX, carrotY, backward);
        }

        drivePow = drivePID.calculateOutput(lateralError);

        // --- Centripetal slip cap: v_max = sqrt(grip * |r|). Skip right at the end. ---
        if (bp.leadChasePowerLimit > 0 && (not closeEnd || distEnd >= 0.7)) {
            double maxSlip = sqrt(bp.leadChasePowerLimit * fabs(chassis.travelArcRadius()));
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
        if (closeEnd && distEnd < 0.7) {
            drivePow *= cos(TO_RAD(chassis.wrappedPointHeadingError(x, y, backward)));
        }

        headingPow = headingPID.calculateOutput(headingError);

        blendOutputs({.cosineScaling = false, .turnPriority = 0});
        governOutputs();
        voltageDrive(leftOut, rightOut);

        exitConditionFulfilled = checkLateralExit(ec, exitStateVars, lateralError, x, y, backward);

        delay(10);
    }
    voltageDrive(0,0); //Stop the motors when the movement ends
}

void voldrive(double left, double right) {
    voltageDrive(left,right);
}

void voldrive(double volts) {
    voltageDrive(volts, volts);
}