#pragma once

#include "exitConditions.h"
#include "blendSettings.h"
#include "odometry.h"
#include "pid.h"
#include "totalOutput.h"

// =========================================================================
// Turn movements
// =========================================================================

void face(double heading,
          TurnExitConditions ec = {},
          int direction = 0);

void facePoint(double x, double y,
               TurnExitConditions ec = {},
               bool backward = false,
               int direction = 0);

void swingOnLeft(double heading,
                 TurnExitConditions ec = {},
                 double partiality = 0,
                 int direction = 0);

void swingOnRight(double heading,
                  TurnExitConditions ec = {},
                  double partiality = 0,
                  int direction = 0);

void swingOnLeftToPoint(double x, double y,
                        TurnExitConditions ec = {},
                        bool backward = false,
                        double partiality = 0,
                        int direction = 0);

void swingOnRightToPoint(double x, double y,
                         TurnExitConditions ec = {},
                         bool backward = false,
                         double partiality = 0,
                         int direction = 0);

// =========================================================================
// Lyapunov control mode
// =========================================================================

extern bool   useLyapunovControl;
extern bool   useClassicControl;
extern bool   useConvergeControl;
extern double kLat;

double sinc(double x);

void lyapunovCalculate(double tx, double ty, bool backward = false);

// =========================================================================
// Boomerang point
// =========================================================================

struct BoomParams {
    double x       = 0;
    double y       = 0;
    double heading = 0;
    double lead    = 0.5;
    bool backward = false;
};

// =========================================================================
// Lateral movements
// =========================================================================

void driveForward(double distance,
                  LateralExitConditions ec = {},
                  BlendSettings bs = {},
                  double heading = chassis.h);

void driveBackward(double distance,
                   LateralExitConditions ec = {},
                   BlendSettings bs = {},
                   double heading = chassis.h);

void moveTo(double x, double y,
            LateralExitConditions ec = {},
            bool backward = false,
            BlendSettings bs = {});

void boomerangTo(BoomParams bp,
                 LateralExitConditions ec = {},
                 bool backward = false,
                 double n = 1.0,
                 BlendSettings bs = {});

double getBoomLeadX(BoomParams bp,
                    bool backward = false);

double getBoomLeadY(BoomParams bp,
                    bool backward = false);

// =========================================================================
// Movement-internal telemetry globals (exposed for debugging / logging)
// =========================================================================

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
extern bool   exitConditionFulfilled;
