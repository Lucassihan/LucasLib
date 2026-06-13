#include "movement/movement.h"
#include "movement/totalOutput.h"
#include "movement/pid.h"
#include "coreConfigs.h"
#include "misc/util.h"

#include <cmath>
#include <algorithm>

double turnPow;
double swingPow;
double drivePow;
double headingPow;
double driveOut;
double turnOut;
double leftOut;
double rightOut;
double headingError;
double lateralError;
double targetX;
double targetY;
double targetH;
double targetStraightDistance;
bool close;
bool exitConditionFulfilled;
double initialHeading;

//Stage 2 governor — applies to combined left/right power. Tunable by user.
TotalOutput totalOutput;

//Slew state for total-output governor. Persists across movements so transitions are smooth.
double lastLeftOut  = 0;
double lastRightOut = 0;

extern double lyapunovMinDist;

void clearAllValues() {
    turnPow = 0;
    swingPow = 0;
    drivePow = 0;
    headingPow = 0;
    driveOut = 0;
    turnOut = 0;
    leftOut = 0;
    rightOut = 0;
    headingError = 0;
    lateralError = 0;
    initialHeading = 0;
    targetX = 0;
    targetY = 0;
    targetH = 0;
    targetStraightDistance = 0;

    close = false;
    exitConditionFulfilled = false;
    lyapunovMinDist = 1e9;

    turnPID.clearAllValues();
    swingPID.clearAllValues();
    headingPID.clearAllValues();
    drivePID.clearAllValues();

    chassis.clearTrackedInches();
}

//Expresses the user's blend preferences (cosine, turn-priority) and writes leftOut/rightOut globals.
//Reads drivePow, headingPow, headingError. Does NOT enforce output constraints (maxOut, minOut, accel/decel rate) — that's governOutputs' job.
//Needs to take in BlendSettings as an argument so it is re-initialized every time the function is called, so the values start as default every time.
void blendOutputs(BlendSettings bs) {
    //start from the raw PID outputs (drivePow / headingPow stay untouched for telemetry/debug)
    driveOut = drivePow;
    turnOut  = headingPow;

    //step 1. Cosine scaling — slows drive when robot is pointed wrong.
    //-if heading error is small, drive output will be large.
    //-if heading error is large, drive output will be small.
    //-if heading error is past 90 degs, cosine = negative, and the drive will start by driving backward while resolving heading error
    if (bs.cosineScaling) {
        driveOut *= cos(headingError * M_PI / 180.0);
    }

    //step 2. Partial turn-priority subtraction — drive absorbs (turnPriority * overturn) of the cut.
    double overturn = fabs(driveOut) + fabs(turnOut) - totalOutput.maxOutput;
    if (overturn > 0) {
        driveOut -= overturn * bs.turnPriority * sign(driveOut);
    }

    //step 3. Blend into left/right voltages. May exceed totalOutput.maxOutput — governOutputs caps it.
    leftOut  = driveOut + turnOut;
    rightOut = driveOut - turnOut;
}

//Slews a single output value against accel/decel limits. Helper for governOutputs.
double slewOutput(double raw, double lastOutput, TotalOutput& totalOut) {
    return slew(raw, lastOutput, totalOut.maxAccelRate, totalOut.maxDecelRate);
}

//Stage 2 governor — applies all four knobs (slew, max, min) to leftOut/rightOut globals.
//Slew is per-side. Max is ratio-preserving (proportional). Min is per-side.
//Reads totalOutput, lastLeftOut, lastRightOut. Writes leftOut, rightOut.
void governOutputs() {
    //slew per side
    leftOut  = slewOutput(leftOut,  lastLeftOut,  totalOutput);
    rightOut = slewOutput(rightOut, lastRightOut, totalOutput);

    //ratio-preserving proportional max cap. The AVERAGE of both sides cannot exceed totalOutput.maxOutput, 
    //but one side can be at max while the other is lower, preserving the turn+drive ratio. 
    //If both sides are over max, they will be scaled down proportionally so the stronger side is at max and the weaker side is under max but maintains the same ratio.
    if (totalOutput.maxOutput > 0) {
        double maxMag = std::max(fabs(leftOut), fabs(rightOut));
        if (maxMag > totalOutput.maxOutput) {
            leftOut  *= totalOutput.maxOutput / maxMag;
            rightOut *= totalOutput.maxOutput / maxMag;
        }
    }

    //min floor per side — ratio breaks at low values, but that's expected for static-friction overcoming
    if (totalOutput.minOutput > 0) {
        if (fabs(leftOut) < totalOutput.minOutput && leftOut != 0) {
            leftOut = sign(leftOut) * totalOutput.minOutput;
        }
        if (fabs(rightOut) < totalOutput.minOutput && rightOut != 0) {
            rightOut = sign(rightOut) * totalOutput.minOutput;
        }
    }

    //update slew state for next iteration
    lastLeftOut  = leftOut;
    lastRightOut = rightOut;
}

//Resolves turn direction when direction is not 0. Prevents the robot from turning ALL the way around again when turnPID slightly overshoots.
double resolveTurnDirection(double wrapped, double positive, double negative, int& direction) {
    if (direction == 1) {
        if (positive < 179) {
            direction = 0;
        } else {
            return positive;
        }
    }
    if (direction == -1) {
        if (negative > -179) {
            direction = 0;
        } else {
            return negative;
        }
    }
    return wrapped;
}

//Resolves the directional heading error for a target heading. Flips the target by 180° if backward.
double resolveHeadingTarget(double heading, bool backward, int& direction) {
    return resolveTurnDirection(chassis.wrappedHeadingError(heading, backward),
                                chassis.positiveHeadingError(heading, backward),
                                chassis.negativeHeadingError(heading, backward),
                                direction);
}

//Resolves the directional heading error for a target point. Flips the target by 180° if backward.
double resolvePointTarget(double targetX, double targetY, bool backward, int& direction) {
    return resolveTurnDirection(chassis.wrappedPointHeadingError(targetX, targetY, backward),
                                chassis.positivePointHeadingError(targetX, targetY, backward),
                                chassis.negativePointHeadingError(targetX, targetY, backward),
                                direction);
}

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
        turnPow = turnPID.calculateOutput(headingError);

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
        turnPow = turnPID.calculateOutput(headingError);

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
            swingPow = turnPID.calculateOutput(headingError);
        } else {
            swingPow = swingPID.calculateOutput(headingError);
        }

        leftOut = -swingPow * partiality;
        rightOut = -swingPow;

        voltageDrive(leftOut, rightOut, true);

        exitConditionFulfilled = checkTurnExit(ec, exitStateVars, headingError);

        delay(10);
    }
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
            swingPow = turnPID.calculateOutput(headingError);
        } else {
            swingPow = swingPID.calculateOutput(headingError);
        }

        leftOut = swingPow;
        rightOut = swingPow * partiality;

        voltageDrive(leftOut, rightOut, true);

        exitConditionFulfilled = checkTurnExit(ec, exitStateVars, headingError);

        delay(10);
    }
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
            swingPow = turnPID.calculateOutput(headingError);
        } else {
            swingPow = swingPID.calculateOutput(headingError);
        }

        leftOut = -swingPow * partiality;
        rightOut = -swingPow;

        voltageDrive(leftOut, rightOut);

        exitConditionFulfilled = checkTurnExit(ec, exitStateVars, headingError);

        delay(10);
    }
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
            swingPow = turnPID.calculateOutput(headingError);
        } else {
            swingPow = swingPID.calculateOutput(headingError);
        }

        leftOut = swingPow;
        rightOut = swingPow * partiality;

        voltageDrive(leftOut, rightOut);

        exitConditionFulfilled = checkTurnExit(ec, exitStateVars, headingError);

        delay(10);
    }
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
}

double getBoomLeadX(BoomParams bp, bool backward) {
    double headingRad = TO_RAD(bp.heading);
    double initialLeadDist = bp.lead * fabs(chassis.distanceTo(bp.x, bp.y));
    double leadSign = -1.0;
    if (backward) {
        leadSign = 1.0;
    }
    return bp.x + leadSign * sin(headingRad) * initialLeadDist;
}

double getBoomLeadY(BoomParams bp, bool backward) {
    double headingRad = TO_RAD(bp.heading);
    double initialLeadDist = bp.lead * fabs(chassis.distanceTo(bp.x, bp.y));
    double leadSign = -1.0;
    if (backward) {
        leadSign = 1.0;
    }
    return bp.y + leadSign * cos(headingRad) * initialLeadDist;
}

void boomerangTo(BoomParams bp,
                 LateralExitConditions ec, bool backward,
                 double n, BlendSettings bs) {
    double x = bp.x;
    double y = bp.y;
    double heading = bp.heading;
    double dLead = bp.lead;
    ExitStateVars exitStateVars;
    clearAllValues();
    targetX = x;
    targetY = y;
    targetH = heading;

    double headingRad = TO_RAD(heading);

    // Flip lead side for backward so the robot backs in and the HEAD
    // still ends up facing `heading` (absolute heading convention).
    double leadSign = -1.0;
    if (backward) {
        leadSign = 1.0;
    }

    double initialLeadDist = dLead * fabs(chassis.distanceTo(x, y));
    double leadX = x + leadSign * sin(headingRad) * initialLeadDist;
    double leadY = y + leadSign * cos(headingRad) * initialLeadDist;

    double dirX = sin(headingRad);
    double dirY = cos(headingRad);
    double initialPerpDist = fabs((chassis.x - x) * dirY - (chassis.y - y) * dirX);
    double minPerpDist = initialPerpDist;

    while (not exitConditionFulfilled) {
        if (useLyapunovControl) {
            // Update lead point retraction
            double perpDist = fabs((chassis.x - x) * dirY - (chassis.y - y) * dirX);
            if (perpDist < minPerpDist) {
                minPerpDist = perpDist;
            }

            double retraction = 0;
            if (initialPerpDist > 0) {
                retraction = pow(minPerpDist / initialPerpDist, n);
            }

            double leadDisplacement = retraction * initialLeadDist;
            leadX = x + leadSign * sin(headingRad) * leadDisplacement;
            leadY = y + leadSign * cos(headingRad) * leadDisplacement;

            // Pursue the lead point with Lyapunov controller
            lyapunovCalculate(leadX, leadY, backward);

            // Telemetry uses distance to actual target, not lead point
            lateralError = chassis.distanceTo(x, y, backward);
        } else {
            if (not close) {
                double perpDist = fabs((chassis.x - x) * dirY - (chassis.y - y) * dirX);
                if (perpDist < minPerpDist) {
                    minPerpDist = perpDist;
                }

                double retraction = 0;
                if (initialPerpDist > 0) {
                    retraction = pow(minPerpDist / initialPerpDist, n);
                }

                double leadDisplacement = retraction * initialLeadDist;
                leadX = x + leadSign * sin(headingRad) * leadDisplacement;
                leadY = y + leadSign * cos(headingRad) * leadDisplacement;

                lateralError = chassis.distanceTo(x, y, backward);

                if (hypot(leadX - chassis.x, leadY - chassis.y) > 0.5) {
                    headingError = chassis.wrappedPointHeadingError(leadX, leadY, backward);
                } else {
                    // Absolute heading: always steer the HEAD to `heading`,
                    // regardless of backward.
                    headingError = chassis.wrappedHeadingError(heading, false);
                }

                if (fabs(lateralError) < 6) {
                    close = true;
                    targetStraightDistance = lateralError;
                    chassis.clearTrackedInches();
                }
            }

            if (close) {
                // Absolute heading: HEAD faces `heading`, even when backward.
                headingError = chassis.wrappedHeadingError(heading, false);
                lateralError = targetStraightDistance - chassis.yInchesTracked;
            }

            drivePow   = drivePID.calculateOutput(lateralError);
            headingPow = headingPID.calculateOutput(headingError);

            blendOutputs(bs);
        }

        governOutputs();
        voltageDrive(leftOut, rightOut);

        exitConditionFulfilled = checkLateralExit(ec, exitStateVars, lateralError, x, y, backward);

        delay(10);
    }
}

// =========================================================================
// Lyapunov control
// =========================================================================

bool   useLyapunovControl = false;
double kLat = 0.04;

double sinc(double x) { //Simple helper function for the sinc function. Uses taylor series approximation when x is small, so we don't accidentally divide by 0.
    if (fabs(x) < 1e-3) {
        return 1.0 - (x * x) / 6.0;
    }
    return sin(x) / x;
}

// Computes leftOut/rightOut using the Lyapunov-based coupled controller.
// Replaces the separate drivePID → headingPID → blendOutputs chain.
// Writes drivePow, headingPow, driveOut, turnOut, leftOut, rightOut,
// headingError, and lateralError for telemetry.
double lyapunovHeadingDisableDist = 3;
double lyapunovMinDist = 1e9;
void lyapunovCalculate(double tx, double ty, bool backward) {
    // 1. World-frame error
    double dx = tx - chassis.x;
    double dy = ty - chassis.y;

    // 2. Rotate into robot's local frame (heading 0 = +Y, 90 = +X).
    //    For backward: rotate heading by π so the robot's back becomes
    //    the reference "forward" direction in the local frame.
    double h = chassis.h * M_PI / 180.0;
    if (backward) {
        h += M_PI;
    }
    double e_x =  sin(h) * dx + cos(h) * dy;
    double e_y = -cos(h) * dx + sin(h) * dy;

    // 3. Local heading error
    double e_theta = atan2(e_y, e_x);

    // 4. Desired drive power (PID on signed distance)
    double dist = hypot(e_x, e_y);
    double distSigned = dist * sign(cos(e_theta));
    double v_d = drivePID.calculateOutput(distSigned);

    // 5. Forward power (cosine-scaled).
    //    For backward: negate v so "forward in the rotated frame"
    //    becomes physical reverse.
    double v = fabs(cos(e_theta)) * v_d;
    if (backward) {
        v = -v;
    }

    // 6. Turn power (Lyapunov math convention: positive ω = CCW).
    //    Negate to match VEX motors where positive turn = CW.
    //    Coupling term uses cosine-scaled v (not raw PID output or
    //    chassis.velocity). This self-scales: v ≈ 0 when heading error
    //    is large (coupling stays quiet, headingPID handles the big turn),
    //    v is full when aligned (coupling actively corrects lateral drift),
    //    v → 0 at the target (no phantom spot turns).
    double omega = headingPID.calculateOutput(e_theta * 180.0 / M_PI)
                 + kLat * v * e_y * sinc(e_theta);
    omega = -omega;

    // One-way latch: once the robot gets close, heading fade only
    // gets stronger (never recovers), preventing atan2 noise from
    // re-engaging heading correction after an overshoot.
    if (dist < lyapunovMinDist) {
        lyapunovMinDist = dist;
    }
    double headingFade = lyapunovMinDist / lyapunovHeadingDisableDist;
    if (headingFade > 1.0) {
        headingFade = 1.0;
    }
    omega *= headingFade;

    // 7. Blend to left/right
    leftOut  = v + omega;
    rightOut = v - omega;

    // Write telemetry globals
    lateralError = dist;
    headingError = e_theta * 180.0 / M_PI;
    drivePow     = v_d;
    headingPow   = omega;
    driveOut     = v;
    turnOut      = omega;
}


double headingDisableDist = 6; //only relevant for non-Lyapunov movement. This is EXACTLY what Lyapunov control fixes
void moveTo(double x, double y, LateralExitConditions ec, bool backward, BlendSettings bs) {
    ExitStateVars exitStateVars;
    clearAllValues();
    targetX = x;
    targetY = y;

    headingError = chassis.wrappedPointHeadingError(targetX, targetY, backward);
    lateralError = chassis.distanceTo(targetX, targetY, backward);

    while (not exitConditionFulfilled) {
        if (useLyapunovControl) {
            lyapunovCalculate(targetX, targetY, backward);
        }

        if (not useLyapunovControl) {
            //if we're far from the target, use point based heading error and distance.
            if (not close) {
                headingError = chassis.wrappedPointHeadingError(targetX, targetY, backward);
                lateralError = chassis.distanceTo(targetX, targetY, backward);

                //check if we're close or not
                if (fabs(lateralError) < headingDisableDist) {
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

            drivePow   = drivePID.calculateOutput(lateralError);
            headingPow = headingPID.calculateOutput(headingError);

            //blend drive + turn into left/right voltages (cosine + turn-priority + blend)
            blendOutputs(bs);
        }

        //enforce output constraints (slew, ratio-preserving max cap, min floor)
        governOutputs();

        //apply to the motors
        voltageDrive(leftOut, rightOut);

        //check exit conditions
        exitConditionFulfilled = checkLateralExit(ec, exitStateVars, lateralError, targetX, targetY, backward);

        delay(10);
    }
}

