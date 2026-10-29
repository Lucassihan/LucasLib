#pragma once

#include "main.h"
#include "exitConditions.h"
#include "movementUtils.h"
#include "pid.h"
#include "scoringControl/dispatchThread.h"

extern PID clawPID;
struct Claw {
    double target;
    double error;
    double currentAngle;
    double lastAngle;
    double clawSpeed;
    double v = 0;            // deg/s, updated each update()
    bool   settled = false;  // |v| < 5 and |error| < 5
    double output;
    double feedForwardTerm;
    double exitError;
    bool asyncControl;

    double clawMax = 60;
    double clawMin = -90;
    double gearRatio = 12.0/48.0;
    double cogOffset = -2;

    double kFeedForward = 1.7;

    double wrapAngle(double angle);
    void   clear();
    void   calculateAngle();
    void   update();
    void   move(double targetAngle);
    void   waitUntil(double errorLessThan);
    void   spit(double volts = 12);
    void   intake(double volts = 12);
    void   stop();
    void   setPosition(double angle);
    void   checkCalibration();
};

extern Claw claw;

extern PID liftPID;
struct Lift {
    double target;
    double error;
    double output;
    double feedForwardTerm;
    double exitError;
    double currentHeight = 0.0;
    double lastHeight = 0;
    // double winchRadius = 7.5/8.0;
    double winchRadius = 8.0/7.5;
    double liftSpeed;
    double v = 0;            // in/s, updated each update()
    bool   settled = false;  // |v| < 5 and |error| < 5
    double liftMax = 50;
    bool asyncControl;

    double kFeedForward = 1.5;

    void clear();
    void calculateHeight();
    void update();
    void move(double targetHeight);
    void waitUntil(double errorLessThan);
    void setPosition(double height);
    void checkCalibration();
};

extern Lift lift;