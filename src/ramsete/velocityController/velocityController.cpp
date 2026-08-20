// velocityController.cpp — one channel of the velocity layer.
// The two instances live in config.cpp; trajectory mixes their
// outputs through the library's own blendOutputs / governOutputs pipeline.

#include "ramsete/velocityController.h"
#include "misc/util.h"

#include <cmath>
#include <algorithm>

VelocityController::VelocityController()
    : error(0), output(0), maxIntegralOutput(2.0),
      kV(0), kA(0), kS(0), kP(0), kI(0),
      source(nullptr), sourceScale(1),
      sumError(0), lastTarget(0), lastTime(0), firstCycle(true) {}

void VelocityController::setConstants(double kV, double kA, double kS, double kP, double kI) {
    this->kV = kV;
    this->kA = kA;
    this->kS = kS;
    this->kP = kP;
    this->kI = kI;
}

void VelocityController::setIntegralSettings(double maxIntegralOutput) {
    this->maxIntegralOutput = maxIntegralOutput;
}

void VelocityController::setSource(double* source, double scale) {
    this->source = source;
    sourceScale = scale;
}

void VelocityController::clearAllValues() {
    error      = 0;
    output     = 0;
    sumError   = 0;
    lastTarget = 0;
    firstCycle = true;
}

double VelocityController::timeSinceLastCycle() {
    double now = vex::timer::systemHighResolution() / 1000000.0;
    double dt  = now - lastTime;

    lastTime = now;
    return dt;
}

double VelocityController::calculateOutput(double target) {
    double dt = timeSinceLastCycle();

    // The first tick after a reset has no previous command to difference
    // against, so it reports no acceleration rather than an infinite step.
    if (firstCycle) {
        lastTarget = target;
        dt         = 0;
        firstCycle = false;
    }

    // Acceleration is the finite difference of consecutive COMMANDS, never of
    // measurements: feedforward should deliver what the plan asked for, not a
    // derivative of encoder noise.
    double accel = dt > 1e-6 ? (target - lastTarget) / dt : 0;
    lastTarget   = target;

    // No source wired up means no feedback: error stays 0 and the channel is
    // pure feedforward.
    double current = source ? *source * sourceScale : target;
    error = target - current;

    sumError += kI * error * dt;
    sumError  = takeSmallerBetween(sumError, maxIntegralOutput);

    output = kV * target + kS * sign(target) + kA * accel + kP * error + sumError;
    return output;
}
