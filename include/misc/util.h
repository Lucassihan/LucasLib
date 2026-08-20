#pragma once

#include "vex.h"

using namespace vex;

// ---- Geometry ----

// A field position, and optionally a facing. Plenty of callers only need x/y — a bezier control point has no heading of its own.
struct Pose {
    double x = 0;
    double y = 0;
    double h = 0;   // degrees, 0 = +Y, clockwise positive
};

// A direction and magnitude rather than a place: a derivative, an offset, a velocity.
struct Vector2 {
    double x = 0;
    double y = 0;
};

// ---- Angle conversions ----

double TO_RAD(double n);
double TO_DEG(double n);

// ---- Sign + magnitude helpers ----

int    sign(double input);
double takeLargerBetween(double raw, double min);
double takeSmallerBetween(double raw, double max);

// sin(x)/x, which is 1 at x = 0. The Taylor form near zero keeps it finite where the plain quotient divides two vanishing numbers.
double sinc(double x);

// ---- Filtering ----

// Mean of the last five cycles, newest first. Deliberately unwired: it exists for the case where
// chassis.v is too noisy for the velocity channel's kP to sit on, which is a real risk but not
// what the drive is currently doing. To bring it back, keep five samples in VelocityController and
// smooth the `current` it reads off the source — nothing else has to change.
// Costs (N-1)/2 = 2 cycles of delay, so filter the measurement, never the command.
double smooth(double current, double last, double last2, double last3, double last4);

// ---- Slew rate limiter ----

double slew(double raw, double last, double maxAccelRate, double maxDecelRate);

// ---- Drive output + timing ----

void voltageDrive(double left, double right, bool offMotorBrake = false);
void delay(double ms);
