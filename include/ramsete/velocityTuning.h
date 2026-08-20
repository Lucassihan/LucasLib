#pragma once

#include "misc/util.h"

#include <vector>

// velocityTuner — runs a ladder of voltage trials and logs each rise to the SD card (lin.txt / ang.txt); the debugger app fits the gains.

enum class SweepAxis { Linear, Angular };

struct SweepChannel {
    SweepChannel(SweepAxis axis, double drift, const char* prefix);

    // A runs the next trial, up/down flips direction, B aborts. An aborted or unsettled trial writes nothing and reruns at the same voltage.
    void collectData(double startVolts, double stepSize, double timeout, double minDwell);

    void display();            // paces itself at 100 ms — use it as the whole controllerScreenTask loop body

    // Settled = the mean of the last `window` samples differs from the previous window's by less than drift.
    double drift;               // in/s linear, deg/s angular
    int    window     = 25;

    int    trial      = 1;      // only a completed trial advances it
    bool   positive   = true;   // forward, or clockwise
    double nextVolts  = 0;
    double lastLogged = 0;      // settled speed of the last completed trial

private:
    void runTrial(double volts, double timeout, double minDwell);
    void record(double volts, const std::vector<Vector2>& rise);

    double      measure() const;      // in/s off chassis.v, or deg/s off chassis.ω
    void        drive(double volts) const;
    const char* unit() const;

    SweepAxis   axis;
    const char* prefix;

    bool fileStarted = false;   // the first write lays down the header
    bool aPrev       = false;
    bool upPrev      = false;
    bool downPrev    = false;
};

struct VelocityTuner {
    SweepChannel linear{SweepAxis::Linear, 0.35, "lin"};
    SweepChannel angular{SweepAxis::Angular, 2.50, "ang"};
};

extern VelocityTuner velocityTuner;

// One-shot self-test for the SD card, for initialize(). Exercises the exact
// path the tuner writes through — stdio fopen/fprintf, not the VEX SDK
// helpers — then reads the bytes back and compares. Reports each stage on the
// brain screen and returns true only if the round trip matched.
bool sdCheck();
