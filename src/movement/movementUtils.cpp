// movementUtils.cpp
// Shared machinery the movements in movement.cpp are built on: global motion
// state + reset, the output pipeline (blend → slew → govern), the heading-error
// resolvers. These are not movements themselves — movement.cpp calls into them.

#include "movement/movementUtils.h"
#include "movement/movement.h"
#include "movement/totalOutput.h"
#include "movement/pid.h"
#include "coreConfigs.h"
#include "misc/util.h"
#include "debugger/debugger.h"

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
Pose   target;
double targetStraightDistance;
bool close;
bool exitConditionFulfilled;
double initialHeading;
double lastV;
double lastω;

// Global drive direction — persists across movements. Initializes false; no
// movement resets it (clearAllValues deliberately leaves it untouched).
bool backward = false;

//Stage 2 governor — applies to combined left/right power. Tunable by user.
TotalOutput totalOutput;

//Slew state for total-output governor. Persists across movements so transitions are smooth.
double lastLeftOut  = 0;
double lastRightOut = 0;

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
    target = {};
    targetStraightDistance = 0;
    lastV = 0;
    lastω = 0;

    close = false;
    exitConditionFulfilled = false;

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
        driveOut *= cos(TO_RAD(headingError));
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
        if (fabs(leftOut) < totalOutput.minOutput and leftOut != 0) {
            leftOut = sign(leftOut) * totalOutput.minOutput;
        }
        if (fabs(rightOut) < totalOutput.minOutput and rightOut != 0) {
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

//Resolves the directional heading error for a target heading.
double resolveHeadingTarget(double heading, int& direction) {
    return resolveTurnDirection(chassis.wrappedHeadingError(heading),
                                chassis.positiveHeadingError(heading),
                                chassis.negativeHeadingError(heading),
                                direction);
}

// =========================================================================
// Boomerang lead-point math
// =========================================================================

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

bool crossed(double x, double y, double crossD) {

    double dx = x - chassis.x;
    double dy = y - chassis.y;
    double hRad = chassis.h * (M_PI / 180.0);
    double along   = dx * sin(hRad) + dy * cos(hRad);
    double lateral = dx * cos(hRad) - dy * sin(hRad);

    if (backward) along = -along;

    return along <= crossD;
}