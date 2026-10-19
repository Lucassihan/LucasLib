#pragma once

#include <cstdint>

// Velocity estimation ported from sylib (github.com/sy1vi3/sylib, MPL-2.0).
//
// A V5 device reports its telemetry to the brain every 10ms, but updates it internally about
// every 5ms — "about", because the device's clock drifts from the brain's by roughly 1 part in
// 95. Every so often that drift crosses the reporting boundary and one 10ms window carries 5ms
// of counts, with a later one carrying 15ms. Dividing counts by the BRAIN's clock therefore
// reads 0.5x, then 1.5x, of the real speed, over and over. Dividing by the difference in the
// DEVICE's own timestamps reads the truth, because that is the interval the counts came from.
//
// Everything here is single-instance: the state lives at file scope in the .cpp, so there is one
// of each filter and one estimator in the program. That is all chassis.v needs.

// --- Filters ---

double smaFilter3(double raw);            // 3-tap mean, the first pass over raw counts
double medianFilter7(double raw);         // 7-sample median, feeding the acceleration estimate only
double extremaFilter20(double raw);       // largest magnitude of the last 20 samples (~200ms)
double emaFilter(double raw, double kA);  // kA of 1 passes the input through, 0 freezes the output

// Slope per millisecond. dtMs comes from the device clock, not a timer of its own.
double derivativePerMs(double value, double dtMs);

// sylib's EMA gain schedule: heavy smoothing while the speed is steady, near-raw response for the
// ~200ms around an acceleration spike. Written against a full-scale-3600 device, so scale the
// acceleration to that range before calling.
double emaGain(double accelPerMs);

// --- Estimator ---

// Filtered encoder speed in position units per second, from raw counts and the device's own
// timestamp in ms. Returns the previous output whenever the device has no new sample.
//   position           counts or degrees, whatever the device reads in
//   deviceTimestamp    the DEVICE's update clock, not the brain's
//   fullScaleVelocity  what this device reads at free speed, in the same units per second; only
//                      the gain schedule uses it
double encoderVelocity(double position, uint32_t deviceTimestamp, double fullScaleVelocity);

// The same speed with no filtering on it at all — counts over the interval they were counted in.
// What a trace should carry if the question is how noisy the measurement itself is.
double rawEncoderVelocity();

// Drops the filter history and re-seeds on the next sample.
void clearEncoderVelocity();
