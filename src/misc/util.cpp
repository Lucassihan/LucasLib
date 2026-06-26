#include "misc/util.h"
#include "coreConfigs.h"
#include "debugger/debugger.h"

#include <cmath>

double TO_RAD(double n) { // converts the input to radians
  return n * 0.01745329252;
}

double TO_DEG(double n) { // converts the input to degrees
  return n * 57.29577951;
}

int sign(double input) { // returns the sign of an input
  if (input > 0.0) {
    return 1;
  }
  if (input < 0.0) {
    return -1;
  }
  return 0;
}

double takeLargerBetween(double raw, double min) {
  double abs1 = fabs(raw);
  double abs2 = fabs(min);
  if (abs1 > abs2) {
    return raw;
  } else {
    return min * sign(raw);
  }
}

double takeSmallerBetween(double raw, double max) {
  double abs1 = fabs(raw);
  double abs2 = fabs(max);
  if (abs1 < abs2) {
    return raw;
  } else {
    return max * sign(raw);
  }
}

double slew(double raw, double last, double maxAccelRate, double maxDecelRate) {
  if (maxAccelRate <= 0 && maxDecelRate <= 0) {
    return raw;
  }
  double delta = raw - last;
  bool accelerating = (fabs(raw) > fabs(last));
  if (accelerating && maxAccelRate > 0) {
    delta = takeSmallerBetween(delta, maxAccelRate);
  }
  if (!accelerating && maxDecelRate > 0) {
    delta = takeSmallerBetween(delta, maxDecelRate);
  }
  return last + delta;
}

void voltageDrive(double left, double right, bool offMotorBrake) {
  if (left == 0 && offMotorBrake) {
    leftdrive.stop(brake);
    rightdrive.spin(fwd, right, volt);
  } else if (right == 0 && offMotorBrake) {
    rightdrive.stop(brake);
    leftdrive.spin(fwd, left, volt);
  } else {
    leftdrive.spin(fwd, left, volt);
    rightdrive.spin(fwd, right, volt);
  }
  debugger::tick();  // sample a trace tick if capture is armed (no-op otherwise)
}

void delay(double ms) {
  wait(ms, msec);
}
