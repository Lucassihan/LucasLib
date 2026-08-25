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

// Mean of the newest `samples` of the five readings given, newest first; `samples` clamps to 1..5,
// and 1 returns the reading untouched. For a MEASUREMENT the caller feeds back into a gain — the
// case it exists for is chassis.v under the velocity channel's kP, where one raw reading carries
// enough noise to move the drive a couple of volts on its own.
// Costs (samples-1)/2 cycles of delay, so point it at a measurement, never at a command.
double smooth(int samples, double current, double last, double last2, double last3, double last4);

// The last four readings of one channel, so smooth() has something to average. Four slots
// regardless of the configured window, so changing it never has to resize anything.
struct VelocityHistory {
  double last1 = 0;
  double last2 = 0;
  double last3 = 0;
  double last4 = 0;

  // Smoothed value for this reading, oldest sample dropped.
  double push(double reading, int samples) {
    double out = smooth(samples, reading, last1, last2, last3, last4);

    last4 = last3;
    last3 = last2;
    last2 = last1;
    last1 = reading;

    return out;
  }

  void clear() {
    last1 = 0;
    last2 = 0;
    last3 = 0;
    last4 = 0;
  }
};

// ---- Slew rate limiter ----

double slew(double raw, double last, double maxAccelRate, double maxDecelRate);

// ---- Drive output + timing ----

void voltageDrive(double left, double right, bool offMotorBrake = false);
void delay(double ms);
