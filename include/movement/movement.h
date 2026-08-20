#pragma once

#include "exitConditions.h"
#include "blendSettings.h"
#include "odometry.h"
#include "pid.h"
#include "totalOutput.h"
#include "movementUtils.h"   // motion state, output pipeline, resolvers

// ---- Turn movements ----

void face(double heading,
          TurnExitConditions ec = {},
          int direction = 0);

void facePoint(double x, double y,
               TurnExitConditions ec = {},
               int direction = 0);

// Face a boomerang target's lead point (honors the global backward).
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

// ---- Lateral movements ----

// distance is a POSITIVE magnitude in both — direction comes from which one you call, never from the global `backward`.
// heading is held with the NOSE either way; drivePID.targetOffset runs along the direction of travel, so + always overshoots past the target.
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

void boomerangTo(BoomParams bp,
                 LateralExitConditions ec = {});

void voldrive(double left, double right);
void voldrive(double volts);
