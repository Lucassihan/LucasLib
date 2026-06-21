#pragma once

#include "exitConditions.h"
#include "blendSettings.h"
#include "odometry.h"
#include "pid.h"
#include "totalOutput.h"
#include "movementUtils.h"   // shared engine: motion state, output pipeline, resolvers, Lyapunov

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
// Lateral movements
// =========================================================================
// (BoomParams + getBoomLeadX/Y now live in movementUtils.h, included above.)

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

// Path-follow Lyapunov drive to a pose. Always rides the FIXED line through
// (x, y). heading = a value → the line points that way; heading = NAN → the
// bot→target heading is frozen at the first loop and held (straightline-steady,
// consistent final heading). Drives on signed along-line distance, so it backs
// up cleanly on overshoot. heading sits before bs so it can be set on its own.
void lyPose(double x, double y,
            LateralExitConditions ec = {},
            bool backward = false,
            double heading = NAN,
            BlendSettings bs = {});

void boomerangTo(BoomParams bp,
                 LateralExitConditions ec = {},
                 bool backward = false,
                 BlendSettings bs = {});

// Motion-state telemetry globals + the Lyapunov control law now live in
// movementUtils.h (included above), so anything that includes movement.h still
// sees turnPow / leftOut / kLat / lyapunovCalculate / etc.
