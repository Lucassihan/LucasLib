#include "movement/pid.h"
#include "misc/util.h"
#include "coreConfigs.h"

#include <cmath>

//instances
PID drivePID;
PID turnPID;
PID headingPID;
PID swingPID;
PID lyPosePID;
PID fourBarPID;

//constructor
PID::PID()
    : activeProfile(0),
      useTempConstants(false),
      tempKP(0), tempKI(0), tempKD(0),
      sumerror(0), lastError(0), prevOutput(0),
      lastTime(0), firstCycle(true),
      lockedKP(0), lockedKI(0), lockedKD(0),
      minOutput(0), maxOutput(12.0),
      targetOffset(0),
      executingLyapunov(false),
      maxAccelRate(0), maxDecelRate(0),
      integralBound(2), maxIntegralOutput(2), //integral will only start to accumulate when it is within 2 error units, and its max contribution to output is 2 volts.
      error(0), output(0), derivative(0)
{
    for (int i = 0; i < MAX_PROFILES; i++) {
        stepCount[i] = 0;
    }
}

//profile management
void PID::setProfile(int profile) {
    if (profile >= 0 && profile < MAX_PROFILES) {
        activeProfile = profile;
    }
}

//constants management
void PID::addStep(double upperBound, double kP, double kI, double kD) {
    int n = stepCount[activeProfile];
    if (n >= MAX_STEPS) {
        return;
    }
    steps[activeProfile][n] = {upperBound, kP, kI, kD};
    stepCount[activeProfile]++;
}

//integral settings
void PID::setIntegralSettings(double bound, double maxIntegralOutput) {
    integralBound = bound;
    this->maxIntegralOutput = maxIntegralOutput;
}

//temp override
void PID::tempConstants(double kP, double kI, double kD) {
    useTempConstants = true;
    tempKP = kP;
    tempKI = kI;
    tempKD = kD;
}

//reset
void PID::clearAllValues() {
    sumerror = 0;
    lastError = 0;
    prevOutput = 0;
    firstCycle = true;
    useTempConstants = false;
    lockedKP = 0;
    lockedKI = 0;
    lockedKD = 0;
    error = 0;
    output = 0;
    derivative = 0;
    executingLyapunov = false;
}

//determine constants — writes the matched step's gains straight into lockedKP/KI/KD
void PID::determineConstants(double inputError) {
    if (useTempConstants) {
        lockedKP = tempKP;
        lockedKI = tempKI;
        lockedKD = tempKD;
        return;
    }

    int count = stepCount[activeProfile];
    double absError = fabs(inputError);

    for (int i = 0; i < count; i++) {
        if (absError <= steps[activeProfile][i].upperBound) {
            lockedKP = steps[activeProfile][i].kP;
            lockedKI = steps[activeProfile][i].kI;
            lockedKD = steps[activeProfile][i].kD;
            return;
        }
    }

    //fallback: use last step
    if (count > 0) {
        lockedKP = steps[activeProfile][count - 1].kP;
        lockedKI = steps[activeProfile][count - 1].kI;
        lockedKD = steps[activeProfile][count - 1].kD;
    } else {
        lockedKP = 0;
        lockedKI = 0;
        lockedKD = 0;
    }
}

double PID::P() {
    return kP * error;
}

double PID::I() {
    return takeSmallerBetween(kI * sumerror, maxIntegralOutput);
}

double PID::D() {
    return kD * derivative;
}

//calculate
double PID::calculateOutput(double inputError) {
    error = inputError;

    //dt
    double now = Brain.timer(msec);
    double dt = now - lastTime;
    if (firstCycle) {
        dt = 10.0;
    }
    if (dt <= 0) {
        dt = 10.0;
    }
    lastTime = now;

    //set pid constants on first cycle, use them for the entire movement
    if (firstCycle) {
        determineConstants(error);
        firstCycle = false;
    }

    kP = lockedKP;
    kI = lockedKI;
    kD = lockedKD;

    derivative = (error - lastError) / dt;
    sumerror = fabs(error) < integralBound ? error + sumerror : 0;

    //THE PID EQUATION IS HERE!
    double raw = P() + I() + D();

    //accel/decel rate limiting — skipped under lyapunov (governor slews instead)
    if (!executingLyapunov) {
        raw = slew(raw, prevOutput, maxAccelRate, maxDecelRate);
    }

    //maxOutput cap (kept even under lyapunov as a safety clamp)
    raw = takeSmallerBetween(raw, maxOutput); //this function compares magnitude while preserving sign

    //minOutput floor — skipped under lyapunov (it would fight settling)
    if (!executingLyapunov) {
        raw = takeLargerBetween(raw, minOutput); //this function compares magnitude while preserving sign
    }

    lastError = error;
    prevOutput = raw;
    output = raw;

    //mirror internals for the debugger (no effect on control)
    integral = sumerror;
    activeKP = kP;
    activeKI = kI;
    activeKD = kD;

    return raw;
}
