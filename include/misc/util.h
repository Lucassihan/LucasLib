#pragma once

#include "vex.h"

using namespace vex;

// =========================================================================
// Angle conversions
// =========================================================================

double TO_RAD(double n);
double TO_DEG(double n);

// =========================================================================
// Sign + magnitude helpers
// =========================================================================

int    sign(double input);
double takeLargerBetween(double raw, double min);
double takeSmallerBetween(double raw, double max);

// =========================================================================
// Slew rate limiter
// =========================================================================

double slew(double raw, double last, double maxAccelRate, double maxDecelRate);

// =========================================================================
// Drive output + timing
// =========================================================================

void voltageDrive(double left, double right, bool offMotorBrake = false);
void delay(double ms);
