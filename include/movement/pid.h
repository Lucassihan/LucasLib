#pragma once

#include "vex.h"
#include "../misc/util.h"

#define MAX_PROFILES 10
#define MAX_STEPS 10

// Built-in profile slot. Add your own values here (e.g. fast, precise, etc.)
// — they're just ints used to index the steps array, so the enum is purely
// for readability at call sites like turnPID.setProfile(normal).
enum PIDProfile {
    normal = 0
};

struct PIDStep {
    double upperBound;
    double kP;
    double kI;
    double kD;
};

class PID {
    public: // stuff that you can use inside auton.cpp and movement.cpp, such as doing something like drivePID.maxAccelRate = 0.5 in the middle of an auton
    PID();

    void   setProfile(int profile);
    void   addStep(double upperBound, double kP, double kI, double kD);
    void   tempConstants(double kP, double kI, double kD);
    void   setIntegralSettings(double bound, double maxIntegralOutput);
    void   clearAllValues();
    double calculateOutput(double inputError);

    // output limits
    double minOutput;
    double maxOutput;

    // When true, this PID skips its minOutput floor AND its per-PID slew. Set by
    // lyapunov movements (lyPose / lyapunov moveTo) so the controller works with
    // raw drive/heading output and all rate-limiting + flooring happens once, at
    // the governor, on the real left/right voltages. Reset by clearAllValues().
    bool executingLyapunov;

    // accel/decel limits
    double maxAccelRate;
    double maxDecelRate;

    // integral windup limits
    double integralBound;
    double maxIntegral;

    // readable state for exit conditions
    double error;
    double output;
    double derivative;

    // readable state for the debugger: the running integral (sumerror) and the
    // constants locked in for the current movement. Set each calculateOutput().
    double integral;
    double activeKP;
    double activeKI;
    double activeKD;

    private: // stuff that is used by the PID class internally for calculations, and will not be accessible outside
    void determineConstants(double inputError, double& kP, double& kI, double& kD);

    PIDStep steps[MAX_PROFILES][MAX_STEPS];
    int     stepCount[MAX_PROFILES];
    int     activeProfile;

    bool   useTempConstants;
    double tempKP;
    double tempKI;
    double tempKD;

    double lockedKP;
    double lockedKI;
    double lockedKD;

    double   sumerror;
    double   lastError;
    double   prevOutput;
    uint64_t lastTime;
    bool     firstCycle;
};

// =========================================================================
// Shared PID instances (defined in pid.cpp)
// =========================================================================

extern PID drivePID;
extern PID turnPID;
extern PID headingPID;
extern PID swingPID;
extern PID lyPosePID;
