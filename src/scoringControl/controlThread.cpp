#include "scoringControl/controlThread.h"
#include "scoringControl/dispatchThread.h"
#include "util.h"

PID clawPID;
PID liftPID;

Claw claw;
Lift lift;

double Claw::wrapAngle(double angle) {
    while (angle > 180.0) {
        angle -= 360.0;
    }
    while (angle < -180.0) {
        angle += 360.0;
    }
    return angle;
}

void Claw::clear() {
    clawPID.clearAllValues();
}

void Claw::calculateAngle() {
    currentAngle = clawMotor.position(degrees) * gearRatio;
    if (currentAngle > clawMax) { setPosition(clawMax); currentAngle = clawMax; }
    if (currentAngle < clawMin) { setPosition(clawMin); currentAngle = clawMin; }
    if (target > clawMax) target = clawMax;
    if (target < clawMin) target = clawMin;
}

void Claw::update() {
    double prevAngle = currentAngle;
    calculateAngle();

    v = (currentAngle - prevAngle) / 0.01;   // deg/s, control task ticks every 10ms

    error = wrapAngle(target - currentAngle);
    settled = fabs(v) < 0.5 and fabs(error) < 5;
    output = clawPID.calculateOutput(error);

    feedForwardTerm = cos(TO_RAD(currentAngle + cogOffset)) * kFeedForward;

    if (asyncControl) clawMotor.spin(forward, output + feedForwardTerm, volt);
}

void Claw::move(double targetAngle) {
    clear();
    error  = wrapAngle(target - currentAngle);
    target = targetAngle;
}

void Claw::waitUntil(double errorLessThan) {
    while (fabs(error) > errorLessThan) {
        delay(10);
    }
}

void Claw::spit(double volts) {
    clawRollers.spin(reverse, volts, volt);
}

void Claw::intake(double volts) {
    clawRollers.spin(forward, volts, volt);
}

void Claw::stop() {
    clawRollers.stop(brake);
}

void Claw::setPosition(double angle) {
    clawMotor.setPosition(angle / gearRatio, degrees);
}

void Claw::checkCalibration() {
    if (settled and clawCurrentState == Tuck) {
        setPosition(clawMin);
        Controller.rumble(".");
    }
}

void Lift::clear() {
    // liftPID.clearAllValues();
}

void Lift::calculateHeight() {
    currentHeight = TO_RAD(liftMotor.position(degrees)) * winchRadius;
    liftSpeed = currentHeight - lastHeight / 0.01;
    lastHeight = currentHeight;

    if (currentHeight < 0) currentHeight = 0;
    if (currentHeight > liftMax) currentHeight = liftMax;
    if (target > liftMax) target = liftMax; 
}

void Lift::update() {
    double prevHeight = currentHeight;
    calculateHeight();

    v = (currentHeight - prevHeight) / 0.01;   // in/s, control task ticks every 10ms

    error = target - currentHeight;
    settled = fabs(v) < 0.5 and fabs(error) < 5;
    output = liftPID.calculateOutput(error);
    if (output < 0) {
        output = takeSmallerBetween(output, 7);
    }

    feedForwardTerm = kFeedForward;

    if (asyncControl) liftMotor.spin(forward, output + feedForwardTerm, volt);
}

void Lift::move(double targetHeight) {
    clear();
    target = targetHeight;
    error  = target - currentHeight;
}

void Lift::waitUntil(double errorLessThan) {
    while (fabs(error) > errorLessThan) {
        delay(10);
    }
}

void Lift::setPosition(double height) {
    liftMotor.setPosition(TO_DEG(height / winchRadius), degrees);
}

void Lift::checkCalibration() {
    if (settled and clawCurrentState == Open) {
        setPosition(0);
        Controller.rumble(".");
    }
}