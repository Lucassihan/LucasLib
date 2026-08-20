#pragma once

#include "blendSettings.h"
#include "misc/util.h"   // Pose

#include <cmath>

// movementUtils — the shared machinery under movement.cpp: motion state, the output pipeline, and the heading-error resolvers. Not movements themselves.

// -------- Shared motion state (exposed for debugging / logging) --------
extern double turnPow;
extern double swingPow;
extern double drivePow;
extern double headingPow;
extern double driveOut;
extern double turnOut;
extern double leftOut;
extern double rightOut;
extern double headingError;
extern double lateralError;
// Point movements fill x and y, turns fill h, boomerangTo fills all three.
extern Pose   target;
extern double targetStraightDistance;
extern double initialHeading;
extern bool   close;
extern bool   exitConditionFulfilled;

// Previous tick's velocities for the exit conditions' settle test. clearAllValues() zeroes them, so settle needs two real samples and a hot handoff can't settle on tick one.
extern double lastV;
extern double lastω;

// Global drive direction for every point/pose movement. A persistent mode, NOT a per-move argument: no movement ever resets it, including clearAllValues().
extern bool   backward;

// -------- Reset: zeroes all motion state + clears every PID --------
void clearAllValues();

// -------- Output pipeline --------
void blendOutputs(BlendSettings bs);   // cosine scaling + turn priority → leftOut/rightOut
void governOutputs();                  // per-side slew, ratio-preserving max cap, per-side min floor

// -------- Heading-error resolvers (choose a turn direction) --------
double resolveTurnDirection(double wrapped, double positive, double negative, int& direction);
double resolveHeadingTarget(double heading, int& direction);

// -------- Boomerang lead-point math --------
struct BoomParams {
    double x          = 0;
    double y          = 0;
    double heading    = 0;
    double lead       = 0.5;   // carrot lead, as a COEFFICIENT of the initial robot->target distance
    double gLead      = 0.0;   // ghost lead [0,1]: how far the ghost trails behind the carrot
    double leadChasePowerLimit = -1;  // centripetal slip cap (grip constant, output^2/inch); -1 disables
};

double getBoomLeadX(BoomParams bp, bool backward = false);
double getBoomLeadY(BoomParams bp, bool backward = false);

//Lil perp line cross function for u to use
bool crossed(double x, double y, double crossD);
