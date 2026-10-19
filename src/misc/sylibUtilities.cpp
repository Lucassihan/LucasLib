#include "misc/sylibUtilities.h"

#include <algorithm>
#include <cmath>

// All of the estimator's state, at file scope so clearEncoderVelocity() can reach it and the
// functions themselves stay naked.
static double smaWindow[3]     = {0, 0, 0};
static int    smaFilled        = 0;
static double medianWindow[7]  = {0};
static int    medianFilled     = 0;
static double extremaWindow[20] = {0};
static int    extremaFilled    = 0;
static double ema              = 0;
static double lastDerivInput   = 0;
static double derivative       = 0;

static uint32_t lastTimestamp  = 0;
static double   lastPosition   = 0;
static double   rawVelocity    = 0;
static double   outputVelocity = 0;
static bool     seeded         = false;

double smaFilter3(double raw) {
  smaWindow[2] = smaWindow[1];
  smaWindow[1] = smaWindow[0];
  smaWindow[0] = raw;
  if (smaFilled < 3) smaFilled++;

  double total = 0;
  for (int i = 0; i < smaFilled; i++) total += smaWindow[i];
  return total / smaFilled;
}

double medianFilter7(double raw) {
  for (int i = 6; i > 0; i--) medianWindow[i] = medianWindow[i - 1];
  medianWindow[0] = raw;
  if (medianFilled < 7) medianFilled++;

  double sorted[7];
  for (int i = 0; i < medianFilled; i++) sorted[i] = medianWindow[i];
  std::sort(sorted, sorted + medianFilled);

  return sorted[medianFilled / 2];
}

double extremaFilter20(double raw) {
  for (int i = 19; i > 0; i--) extremaWindow[i] = extremaWindow[i - 1];
  extremaWindow[0] = raw;
  if (extremaFilled < 20) extremaFilled++;

  double peak = 0;
  for (int i = 0; i < extremaFilled; i++) peak = std::max(peak, fabs(extremaWindow[i]));
  return peak;
}

double emaFilter(double raw, double kA) {
  ema = raw * kA + ema * (1 - kA);
  return ema;
}

double derivativePerMs(double value, double dtMs) {
  if (dtMs <= 0) return derivative;

  derivative = (value - lastDerivInput) / dtMs;
  lastDerivInput = value;
  return derivative;
}

double emaGain(double accelPerMs) {
  return 0.75 * (1 - (1 / ((accelPerMs * accelPerMs / 50) + 1.013)));
}

double encoderVelocity(double position, uint32_t deviceTimestamp, double fullScaleVelocity) {
  if (not seeded) {
    lastTimestamp = deviceTimestamp;
    lastPosition  = position;
    seeded        = true;
    return 0;
  }

  // The device's clock ticks in 5ms steps but arrives with 4ms and 6ms jitter on it; rounding to
  // the nearest 5 recovers the interval the counts were actually measured over. A dT of 0 means
  // the device has not published anything new, so there is nothing to compute.
  double dT = 5 * std::round((double)(deviceTimestamp - lastTimestamp) / 5.0);
  if (dT <= 0) return outputVelocity;

  double dPosition = position - lastPosition;
  lastTimestamp    = deviceTimestamp;
  lastPosition     = position;

  double raw = dPosition / dT * 1000;

  // Past free speed the reading is not motion, it's someone resetting the encoder. The baselines
  // are already updated, so the next sample measures from the new zero instead of sticking here.
  if (fabs(raw) > fullScaleVelocity * 1.4) return outputVelocity;

  rawVelocity     = raw;
  double smoothed = smaFilter3(raw);

  // The median filter's output feeds the acceleration estimate only — the velocity that leaves
  // this function is the moving average, smoothed by the EMA. Acceleration is a derivative of an
  // already-derived value, so it is noisy enough to need the extra filter; the velocity is not.
  double accel = derivativePerMs(medianFilter7(smoothed), dT);
  double kA    = emaGain(extremaFilter20(accel) * 3600 / fullScaleVelocity);

  outputVelocity = emaFilter(smoothed, kA);
  return outputVelocity;
}

double rawEncoderVelocity() { return rawVelocity; }

void clearEncoderVelocity() {
  for (int i = 0; i < 3; i++)  smaWindow[i] = 0;
  for (int i = 0; i < 7; i++)  medianWindow[i] = 0;
  for (int i = 0; i < 20; i++) extremaWindow[i] = 0;

  smaFilled      = 0;
  medianFilled   = 0;
  extremaFilled  = 0;
  ema            = 0;
  lastDerivInput = 0;
  derivative     = 0;
  rawVelocity    = 0;
  outputVelocity = 0;
  seeded         = false;
}
