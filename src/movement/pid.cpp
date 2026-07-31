#include "movement/pid.h"
#include "misc/util.h"
#include "debugger/debugger.h"

#include <cmath>

//instances
PID drivePID;
PID turnPID;
PID headingPID;
PID swingPID;
PID fourBarPID;

//constructor
PID::PID()
    : activeProfile(0),
      useTempConstants(false),
      tempKP(0), tempKI(0), tempKD(0),
      sumError(0), lastError(0), prevOutput(0),
      lastTime(0), firstCycle(true),
      minOutput(0), maxOutput(12.0),
      targetOffset(0),
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
    if (profile >= 0 and profile < MAX_PROFILES) {
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
    sumError = 0;
    lastError = 0;
    // prevOutput = 0; important: prevOutput is NOT cleared, so slew carries between movements.
    firstCycle = true;
    useTempConstants = false;
    error = 0;
    output = 0;
    derivative = 0;
}

//determine constants — writes the matched step's gains straight into kP/kI/kD.
//Called once on the first cycle; the gains then hold for the whole movement.
void PID::determineConstants(double inputError) {
    if (useTempConstants) {
        kP = tempKP;
        kI = tempKI;
        kD = tempKD;
        return;
    }

    int count = stepCount[activeProfile];
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
        kP = 0;
        kI = 0;
        kD = 0;
    }
}

void PID::governOutput() {
    raw = slew(raw, prevOutput, maxAccelRate, maxDecelRate);

    raw = takeSmallerBetween(raw, maxOutput); //this function compares magnitude while preserving sign

    raw = takeLargerBetween(raw, minOutput); //this function compares magnitude while preserving sign

    output = raw;
}

double PID::timeSinceLastCycle() {
    double now = vex::timer::systemHighResolution() / 1000.0;
    double dt = (now - lastTime);

    lastTime = now;
    return dt;
}

double PID::P() {
    return kP * error;
}

double PID::I() {
    double i = kI * sumError;
    return takeSmallerBetween(i, maxIntegralOutput);
}

double PID::D() {
    return kD * derivative;
}

//calculate
double PID::calculateOutput(double inputError) {
    if (firstCycle) {
        determineConstants(inputError);
        lastError = inputError;
        firstCycle = false;
    }

    error = inputError;                                            // P
    sumError = fabs(error) < integralBound ? error + sumError : 0; // I
    derivative = (error - lastError) / timeSinceLastCycle();       // D

    raw = P() + I() + D();

    governOutput(); //applies slew, maxOutput, and minOutput

    lastError = error;
    prevOutput = raw;

    debugger::samplePID(this, sumError, kP, kI, kD);  //hand internals to the debugger (no-op unless capturing)

    return output;
}
