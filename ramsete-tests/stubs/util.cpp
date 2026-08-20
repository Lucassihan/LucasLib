// Same bodies as src/misc/util.cpp, minus everything that needs the brain.
#include "misc/util.h"
#include <cmath>

double TO_RAD(double n) { return n * 0.01745329252; }
double TO_DEG(double n) { return n * 57.29577951; }

int sign(double input) {
  if (input > 0.0) return 1;
  if (input < 0.0) return -1;
  return 0;
}

double takeLargerBetween(double raw, double min) {
  return fabs(raw) > fabs(min) ? raw : min * sign(raw);
}

double takeSmallerBetween(double raw, double max) {
  return fabs(raw) < fabs(max) ? raw : max * sign(raw);
}

double sinc(double x) {
  if (fabs(x) < 1e-3) return 1.0 - x * x / 6.0;
  return sin(x) / x;
}
