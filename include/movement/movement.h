#pragma once

#include "exitConditions.h"
#include "blendSettings.h"
#include "odometry.h"
#include "pid.h"
#include "totalOutput.h"
#include "movementUtils.h"   // shared engine: motion state, output pipeline, resolvers, Lyapunov
#include "pursuit.h"         // followPath + PathPoint/PursuitParams (pure pursuit)

// =========================================================================
// Turn movements
// =========================================================================

void face(double heading,
          TurnExitConditions ec = {},
          int direction = 0);

void facePoint(double x, double y,
               TurnExitConditions ec = {},
               int direction = 0);

// Overload: face a boomerang target's lead point (honors the global backward).
void facePoint(BoomParams bp,
               TurnExitConditions ec = {},
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
                        double partiality = 0,
                        int direction = 0);

void swingOnRightToPoint(double x, double y,
                         TurnExitConditions ec = {},
                         double partiality = 0,
                         int direction = 0);

// =========================================================================
// Lateral movements
// =========================================================================
// (BoomParams + getBoomLeadX/Y now live in movementUtils.h, included above.)

void driveForward(double distance,
                  double heading = chassis.h,
                  LateralExitConditions ec = {},
                  BlendSettings bs = {});

void driveBackward(double distance,
                   double heading = chassis.h,
                   LateralExitConditions ec = {},
                   BlendSettings bs = {});

void moveTo(double x, double y,
            LateralExitConditions ec = {},
            BlendSettings bs = {});

// Path-follow Lyapunov drive to a pose. Always rides the FIXED line through
// (x, y). heading = a value → the line points that way; heading = NAN → the
// bot→target heading is frozen at the first loop and held (straightline-steady,
// consistent final heading). Drives on signed along-line distance, so it backs
// up cleanly on overshoot.
// Within the lyPoseTurnDisableLength global (default 3") of the pose, steering
// is disabled and the bot drives straight in on the held heading — a clean,
// jitter-free settle (CLOSE stage). Outside it the full path-follow law runs
// (FAR stage). No BlendSettings: the control law does its own cosine scaling
// and left/right blend.
void lyPose(double x, double y,
            LateralExitConditions ec = {},
            double heading = NAN);

void boomerangTo(BoomParams bp,
                 LateralExitConditions ec = {});

void voldrive(double left, double right);
void voldrive(double volts);
