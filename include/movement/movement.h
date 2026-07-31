#pragma once

#include "exitConditions.h"
#include "blendSettings.h"
#include "odometry.h"
#include "pid.h"
#include "totalOutput.h"
#include "movementUtils.h"   // shared engine: motion state, output pipeline, resolvers

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

// Relative straight drive along a held heading. distance is SIGNED: positive
// drives forward, negative backs up. This is the one movement that does NOT
// consult the global `backward` — direction comes from the sign of distance and
// nothing else, so flipping the global has no effect here.
// heading is held with the NOSE either way (a backward drive does not flip it);
// it defaults to whatever chassis.h was at call time.
// drivePID.targetOffset is applied along the direction of travel, so its sign
// means the same thing in both directions: positive overshoots PAST the target,
// negative stops SHORT of it, regardless of which way distance points.
void drive(double distance,
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
