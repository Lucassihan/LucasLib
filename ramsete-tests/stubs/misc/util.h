#pragma once

// Host-build stand-in for include/misc/util.h.
//
// The real header pulls in vex.h, which does not exist off the brain. The math
// files in ramsete/ only ever use these four helpers, so the harness puts this
// directory ahead of include/ on the search path and supplies them itself.
// Definitions live in stubs/util.cpp, copied from src/misc/util.cpp.

// =========================================================================
// Geometry
// =========================================================================

// A field position, and optionally a facing. Plenty of callers only need the
// x/y and leave h at its default — a bezier control point, for instance, has
// no heading of its own.
struct Pose {
    double x = 0;
    double y = 0;
    double h = 0;   // degrees, 0 = +Y, clockwise positive
};

// A direction and magnitude rather than a place: the derivative of a curve,
// an offset, a velocity. Same two numbers as a Pose, different meaning, which
// is why it is a different type.
struct Vector2 {
    double x = 0;
    double y = 0;
};

double TO_RAD(double n);
double TO_DEG(double n);
int    sign(double input);
double takeLargerBetween(double raw, double min);
double takeSmallerBetween(double raw, double max);
double sinc(double x);
