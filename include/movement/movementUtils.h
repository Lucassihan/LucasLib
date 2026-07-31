#pragma once

#include "blendSettings.h"

#include <cmath>

// =========================================================================
// movementUtils — shared machinery the movements are built on.
// Global motion state, the output pipeline (blend → slew → govern), and the
// heading-error resolvers. The user-facing movements in movement.cpp call
// into all of this; these are not movements themselves.
// =========================================================================

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
extern double targetX;
extern double targetY;
extern double targetH;
extern double targetStraightDistance;
extern double initialHeading;
extern bool   close;
extern bool   exitConditionFulfilled;

// Previous tick's chassis velocities, held for the exit conditions' settle test
// ("am I slower than I was?"). clearAllValues() zeroes them, so every movement
// starts from 0 and settle needs two real samples before it can fire — that is
// what stops a hot handoff, where the robot is already decelerating into the
// next movement, from settling on its very first tick.
extern double lastV;
extern double lastω;

// Global drive direction. Every point/pose movement (facePoint-to-point, swing-
// to-point, moveTo, boomerangTo) runs backward while this is true. It is
// a persistent mode, NOT a per-move argument: it initializes to false and NO
// movement ever resets it — clearAllValues() leaves it alone. Flip it yourself to
// change direction; it stays flipped until you flip it back.
extern bool   backward;

// -------- Reset: zeroes all motion state + clears every PID --------
void clearAllValues();

// -------- Output pipeline --------
// blendOutputs: cosine scaling + turn-priority → writes leftOut/rightOut.
// governOutputs: per-side slew, ratio-preserving max cap, per-side min floor.
void blendOutputs(BlendSettings bs);
void governOutputs();

// -------- Heading-error resolvers (choose a turn direction) --------
double resolveTurnDirection(double wrapped, double positive, double negative, int& direction);
double resolveHeadingTarget(double heading, bool backward, int& direction);

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
