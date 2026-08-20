#pragma once

// velocityController — one channel of the velocity layer, turning a RAMSETE command into volts: u = kV·target + kS·sign(target) + kA·accel + kP·error + kI·∫error.
// The gains are MEASURED, not tuned: a voltage sweep fits kV and kS, and one step response fits K, L, T → kA = (L+T)/K, kP = T/(K·λ), kI = kP/T, λ = T/2.

class VelocityController {
public:
    VelocityController();

    // kV volts per unit velocity, kA per unit acceleration, kS to break static friction, kP/kI feedback. kI is per SECOND, not per cycle.
    void setConstants(double kV, double kA, double kS, double kP, double kI);

    // Anti-windup: while the output is saturated the error can't shrink, so cap what the integral may contribute, in volts.
    void setIntegralSettings(double maxIntegralOutput);

    void clearAllValues();

    // The field of `chassis` this channel compares itself against; scale converts it into the target's unit (the turn channel needs 57.3, chassis.ω being rad/s). No source = open loop.
    void setSource(double* source, double scale = 1);

    // Volts for this channel. Reads the speed off the source itself, and derives the acceleration term and dt.
    double calculateOutput(double target);

    // Seconds since the previous call. (PID's version reports milliseconds; these gains are all per-second.)
    double timeSinceLastCycle();

    // readable state, for logging and the exit conditions
    double error;
    double output;
    double maxIntegralOutput;

private:
    double kV;
    double kA;
    double kS;
    double kP;
    double kI;

    double* source;      // the measured speed this channel chases
    double  sourceScale;

    double sumError;     // volts, already scaled by kI
    double lastTarget;
    double lastTime;
    bool   firstCycle;
};
