#pragma once

#include "main.h"
#include "exitConditions.h"
#include "movementUtils.h"
#include "pid.h"

PID clawPID;
struct Claw {
    double target;
    double error;
    double currentAngle;
    double lastAngle;
    double clawSpeed;
    double output;
    double feedForwardTerm;
    double kFeedForward;
    double exitError;
    bool asyncControl;
    bool intaking;
    bool stallDetected;

    double stallVelocity = 20;   // rpm
    double stallTime     = 250;  // ms
    double lastSpinTime;

    double clawMax = 60;
    double clawMin = -90;
    double gearRatio = 24.0/60.0;

    double wrapAngle(double angle) {
        while (angle > 180.0) {
            angle -= 360.0;
        }
        while (angle < -180.0) {
            angle += 360.0;
        }
        return angle;
    }

    void clear() {
        clawPID.clearAllValues();
    }

    void calculateAngle() {
        currentAngle = clawMotor.position(degrees) * gearRatio;
        if (currentAngle > clawMax) clawMotor.setPosition(clawMax, degrees);
        if (currentAngle < clawMin) clawMotor.setPosition(clawMin, degrees);
        if (target > clawMax) target = clawMax;
        if (target < clawMin) target = clawMin;
    }

    void update() {
        calculateAngle();
        error = wrapAngle(target - currentAngle);
        output = clawPID.calculateOutput(error);

        feedForwardTerm = cos(TO_RAD(clawMotor.position(degrees))) * kFeedForward;

        if (asyncControl) clawMotor.spin(forward, output + feedForwardTerm, volt);

        checkStall();
    }

    void move(double targetAngle) {
        clear();
        error  = wrapAngle(target - currentAngle);
        target = targetAngle;
    }

    void waitUntil(double errorLessThan) {
        while (fabs(error) > errorLessThan) {
            delay(10);
        }
    }

    // Timer only resets while the rollers are actually turning, so stallTime is
    // time spent continuously below stallVelocity, not total time intaking.
    void checkStall() {
        if (!intaking) return;

        if (fabs(clawRollers.velocity(rpm)) > stallVelocity) lastSpinTime = Brain.timer(msec);

        if (Brain.timer(msec) - lastSpinTime >= stallTime) {
            stop();
            stallDetected = true;
        }
    }

    void spit(double volts = 12) {
        intaking = false;
        clawRollers.spin(reverse, volts, volt);
    }

    void intake(double volts = 12) {
        clawRollers.spin(forward, volts, volt);

        intaking      = true;
        stallDetected = false;
        lastSpinTime  = Brain.timer(msec);
    }

    void stop() {
        intaking = false;
        clawRollers.stop(brake);
    }

    void setPosition(double angle) {
        clawMotor.setPosition(angle / gearRatio, degrees);
    }
};

PID liftPID;
struct Lift {
    double target;
    double error;
    double output;
    double feedForwardTerm;
    double kFeedForward;
    double exitError;
    double currentHeight = 0;
    double lastHeight = 0;
    double winchRadius = 0.5;
    double liftSpeed;
    double liftMax = 50;
    bool asyncControl;

    void clear() {
        liftPID.clearAllValues();
    }

    void calculateHeight() {
        currentHeight = TO_RAD(liftMotor.position(degrees)) * winchRadius;
        liftSpeed = currentHeight - lastHeight / 0.01;
        lastHeight = currentHeight;

        if (currentHeight < 0) currentHeight = 0;
        if (currentHeight > liftMax) currentHeight = liftMax;
        if (target > liftMax) target = liftMax; 
    }

    void update() {
        calculateHeight();
        error = target - currentHeight;
        output = liftPID.calculateOutput(error);

        feedForwardTerm = kFeedForward;

        if (asyncControl) liftMotor.spin(forward, output + feedForwardTerm, volt);
    }

    void move(double targetHeight) {
        clear();
        target = targetHeight;
        error  = target - currentHeight;
    }

    void waitUntil(double errorLessThan) {
        while (fabs(error) > errorLessThan) {
            delay(10);
        }
    }

    void setPosition(double angle) {
        clawMotor.setPosition(angle / winchRadius, degrees);
    }
};