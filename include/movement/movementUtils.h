#pragma once

#include "blendSettings.h"

#include <cmath>   // NAN (used by lyPose's default heading arg in movement.h)

// =========================================================================
// movementUtils — shared machinery the movements are built on.
// Global motion state, the output pipeline (blend → slew → govern), the
// heading-error resolvers, and the Lyapunov control law. The user-facing
// movements in movement.cpp call into all of this; these are not movements
// themselves.
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

extern double lateralUndershoot;
extern double turnOvershoot;


// -------- Lyapunov control gain (user-tunable) --------
extern double kLat;

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
double resolvePointTarget(double targetX, double targetY, bool backward, int& direction);

// -------- Lyapunov control law (path-follow) --------
double sinc(double x);
// Tracks the FIXED line through (tx, ty) at pathHeading (deg, 0=+Y, CW+):
// e_x = signed along-line distance to the target, e_y = signed perpendicular
// offset from the line, e_theta = robot heading − path heading. Writes
// leftOut/rightOut (+ telemetry). Called every loop by lyPose.
void lyapunovCalculate(double tx, double ty, bool backward, double pathHeading);

// -------- Boomerang lead-point math --------
struct BoomParams {
    double x          = 0;
    double y          = 0;
    double heading    = 0;
    double lead       = 0.5;   // carrot lead, as a COEFFICIENT of the initial robot->target distance
    double gLead      = 0.0;   // ghost lead [0,1]: how far the ghost trails behind the carrot
    double leadChasePowerLimit = -1;  // centripetal slip cap (grip constant, output^2/inch); -1 disables
    bool   backward   = false;
};

double getBoomLeadX(BoomParams bp, bool backward = false);
double getBoomLeadY(BoomParams bp, bool backward = false);
