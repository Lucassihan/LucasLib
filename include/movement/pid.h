#pragma once

#include "vex.h"
#include "../misc/util.h"

#define MAX_PROFILES 10
#define MAX_STEPS 10

// Profile slots. Just ints indexing the steps array — add your own (fast, precise) for readability at call sites like turnPID.setProfile(normal).
enum PIDProfile {
    normal = 0,
    holding = 0
};

struct PIDStep {
    double upperBound;
    double kP;
    double kI;
    double kD;
};

class PID {
    public: // usable from auton.cpp and movement.cpp, e.g. drivePID.maxAccelRate = 0.5 mid-auton
    PID();

    void   setProfile(int profile);
    void   addStep(double upperBound, double kP, double kI, double kD);
    void   tempConstants(double kP, double kI, double kD);
    void   setIntegralSettings(double bound, double maxIntegralOutput);
    void   clearAllValues();
    void governOutput();
    double timeSinceLastCycle();
    double P();
    double I();
    double D();
    double calculateOutput(double inputError);

    // output limits
    double minOutput;
    double maxOutput;

    // Set before a movement to bias it; the movement zeroes it on exit. NOT touched by clearAllValues(), so it survives the start-of-run reset.
    double targetOffset;

    // accel/decel limits
    double maxAccelRate;
    double maxDecelRate;

    // integral windup limits
    double integralBound;
    double maxIntegralOutput;

    // readable state for exit conditions
    double error;
    double output;
    double derivative;

    private: // internal to the calculation
    void determineConstants(double inputError);

    PIDStep steps[MAX_PROFILES][MAX_STEPS];
    int     stepCount[MAX_PROFILES];
    int     activeProfile;

    bool   useTempConstants;
    double tempKP;
    double tempKI;
    double tempKD;

    double kP;
    double kI;
    double kD;

    double   sumError;
    double   lastError;
    double   prevOutput;
    double lastTime;
    bool     firstCycle;
    double   raw;
};

// ---- Shared PID instances (defined in pid.cpp) ----

extern PID drivePID;
extern PID turnPID;
extern PID headingPID;
extern PID swingPID;
extern PID fourBarPID;