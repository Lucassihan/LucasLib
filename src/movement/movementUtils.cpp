// movementUtils.cpp
// Shared machinery the movements in movement.cpp are built on: global motion
// state + reset, the output pipeline (blend → slew → govern), the heading-error
// resolvers, and the Lyapunov control law. These are not movements themselves —
// movement.cpp calls into them.

#include "movement/movementUtils.h"
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

double driveStopShort = 0;
double turnOvershoot = 0;

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
    targetX = 0;
    targetY = 0;
    targetH = 0;
    targetStraightDistance = 0;

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

// =========================================================================
// Lyapunov control
// =========================================================================

double kLat = 0.04;

double sinc(double x) { //Simple helper function for the sinc function. Uses taylor series approximation when x is small, so we don't accidentally divide by 0.
    if (fabs(x) < 1e-3) {
        return 1.0 - (x * x) / 6.0;
    }
    return sin(x) / x;
}

// Path-follow Lyapunov coupled controller. Tracks the FIXED line through
// (tx, ty) at pathHeading, replacing the separate drivePID → headingPID →
// blendOutputs chain. Writes drivePow, headingPow, driveOut, turnOut, leftOut,
// rightOut, headingError, and lateralError for telemetry.
void lyapunovCalculate(double tx, double ty, bool backward, double pathHeading) {
    // 1. World-frame error vector from robot to target.
    double dx = tx - chassis.x;
    double dy = ty - chassis.y;

    // 2. Reference frame = the fixed path line.
    //    For backward: add π so "forward" in the rotated frame is physical reverse.
    double refH = pathHeading * M_PI / 180.0;
    if (backward) {
        refH += M_PI;
    }

    // 3. Rotate (dx, dy) into the path frame.
    //    e_x = signed distance to the target along the line.
    //    e_y = signed perpendicular (lateral) offset from the line.
    double e_x =  sin(refH) * dx + cos(refH) * dy;
    double e_y = -cos(refH) * dx + sin(refH) * dy;

    // 4. Heading error = robot_heading − path_heading, wrapped to [-π, π].
    //    No atan2 singularity near the goal — this is what lets path-follow
    //    settle cleanly.
    double robotH = chassis.h * M_PI / 180.0;
    if (backward) {
        robotH += M_PI;
    }
    double e_theta = robotH - refH;
    while (e_theta > M_PI) {
        e_theta -= 2.0 * M_PI;
    }
    while (e_theta < -M_PI) {
        e_theta += 2.0 * M_PI;
    }

    // 5. Drive power: PID on e_x (signed along-line distance). e_x flips negative
    //    the instant the robot passes the target, so the PID backs it up — no
    //    overshoot — and lateral error never bleeds into the forward command
    //    (the coupling term in step 7 owns lateral recovery).
    double v_d = drivePID.calculateOutput(e_x - driveStopShort);

    // 6. Forward power (cosine-scaled, so the robot slows when badly misaligned).
    //    For backward: negate v so "forward in the rotated frame" is physical reverse.
    double v = fabs(cos(e_theta)) * v_d;
    if (backward) {
        v = -v;
    }

    // 7. Turn power (Lyapunov math convention: positive ω = CCW).
    //    Negate to match VEX motors where positive turn = CW.
    //    Coupling term uses cosine-scaled v, so it self-scales: v ≈ 0 when
    //    heading error is large (coupling stays quiet, headingPID handles the big
    //    turn), full when aligned (actively corrects lateral drift), and → 0 at
    //    the target (no phantom spot turns).
    double omega = lyPosePID.calculateOutput(e_theta * 180.0 / M_PI)
                 + kLat * v * e_y * sinc(e_theta);
    omega = -omega;

    // 8. Blend to left/right.
    leftOut  = v + omega;
    rightOut = v - omega;

    // Write telemetry globals. lateralError stays the TRUE distance to the
    // target (not e_x), so exit conditions settle on the actual pose rather than
    // just the right station along the line.
    double dist = hypot(e_x, e_y);
    lateralError = dist;
    headingError = e_theta * 180.0 / M_PI;
    drivePow     = v_d;
    headingPow   = omega;
    driveOut     = v;
    turnOut      = omega;
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
