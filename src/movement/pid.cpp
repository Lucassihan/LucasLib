#include "movement/pid.h"
#include "misc/util.h"

#include <cmath>

//instances
PID drivePID;
PID turnPID;
PID headingPID;
PID swingPID;

//constructor
PID::PID()
    : activeProfile(0),
      useTempConstants(false),
      tempKP(0), tempKI(0), tempKD(0),
      sumerror(0), lastError(0), prevOutput(0),
      lastTime(0), firstCycle(true),
      lockedKP(0), lockedKI(0), lockedKD(0),
      minOutput(0), maxOutput(12.0),
      maxAccelRate(0), maxDecelRate(0),
      integralBound(2), maxIntegral(2), //integral will only start to accumulate when it is within 2 error units, and its max contribution to output is 2 volts.
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
    maxIntegral = maxIntegralOutput;
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
    sumerror         = 0;
    lastError        = 0;
    prevOutput       = 0;
    firstCycle       = true;
    useTempConstants = false;
    lockedKP         = 0;
    lockedKI         = 0;
    lockedKD         = 0;
    error            = 0;
    output           = 0;
    derivative       = 0;
}

//determine constants
void PID::determineConstants(double inputError, double &kP, double &kI, double &kD) {
    if (useTempConstants) {
        kP = tempKP;
        kI = tempKI;
        kD = tempKD;
        return;
    }

    int    count    = stepCount[activeProfile];
    double absError = fabs(inputError);

    for (int i = 0; i < count; i++) {
        if (absError <= steps[activeProfile][i].upperBound) {
            kP = steps[activeProfile][i].kP;
            kI = steps[activeProfile][i].kI;
            kD = steps[activeProfile][i].kD;
            return;
        }
    }

    //fallback: use last step
    if (count > 0) {
        kP = steps[activeProfile][count - 1].kP;
        kI = steps[activeProfile][count - 1].kI;
        kD = steps[activeProfile][count - 1].kD;
    } else {
        kP = 0; kI = 0; kD = 0;
    }
}

//calculate
double PID::calculateOutput(double inputError) {
    error = inputError;

    //dt
    uint64_t now = vex::timer::systemHighResolution();
    double dt    = firstCycle ? 10.0 : (now - lastTime) / 1000.0;
    if (dt <= 0) {
        dt = 10.0;
    }
    lastTime = now;

    //set pid constants on first cycle, use them for the entire movement
    if (firstCycle) {
        determineConstants(error, lockedKP, lockedKI, lockedKD);
        firstCycle = false;
    }

    double kP = lockedKP;
    double kI = lockedKI;
    double kD = lockedKD;

    //derivative
    derivative = (error - lastError) / dt;

    //integral with anti-windup
    bool saturated = (maxOutput > 0 && fabs(prevOutput) >= maxOutput);
    bool withinBound = (integralBound == 0 || fabs(error) < integralBound);

    if (!saturated && withinBound) {
        sumerror += error;
    }

    //cap integral to maxIntegral so it doesn't cause crazy overshooting
    if (maxIntegral > 0 && kI > 0) {
        double maxSum = maxIntegral / kI;
        if (sumerror > maxSum) {
            sumerror = maxSum;
        } else if (sumerror < -maxSum) {
            sumerror = -maxSum;
        }
    }

    //THE FAMOUS PID EQUATION IS HERE!
    double raw = kP * error + kI * sumerror + kD * derivative;

    //accel/decel rate limiting
    raw = slew(raw, prevOutput, maxAccelRate, maxDecelRate);

    //maxOutput cap
    raw = takeSmallerBetween(raw, maxOutput); //this function compares magnitude while preserving sign

    //minOutput floor   
    raw = takeLargerBetween(raw, minOutput); //this function compares magnitude while preserving sign

    lastError = error;
    prevOutput = raw;
    output = raw;

    return raw;
}
