#pragma once
#include "vex.h"
#include <cmath>

// ============================================================================
// mclConfig.h -- all the MCL knobs as ONE struct, accessed with a dot:
//   MCLConfig.FORWARD_NOISE,  MCLConfig.MCL_WEIGHT,  MCLConfig.maxLateralAccel()
// The actual NUMBERS live in mclConfig.cpp (the global `MCLConfig` instance) so
// you have one place to tune. The two structural constants (NUM_PARTICLES,
// FIELD_HALF) are compile-time `static constexpr` members.
// ============================================================================

struct MclConfig {
  // ---- compile-time structural constants ----
  static constexpr int    NUM_PARTICLES = 400;      // size of the particle array
  static constexpr double FIELD_HALF    = 70.4375;  // inches, center to a wall

  // ---- predict noise ----
  double FORWARD_NOISE;        // along the heading (fraction of distance moved)
  double SIDEWAYS_NOISE;       // base sideways noise (no lateral tracker)
  double SLIP_GAIN;            // EXTRA sideways noise per unit of slip (s^2/in)

  // ---- update gating ----
  double MIN_UPDATE_DIST;      // inches moved before we weight+resample
  double MAX_NO_UPDATE_TIME;  // seconds before we weight+resample anyway

  // ---- distance sensor model ----
  double SENSOR_MAX_RANGE;     // inches; ignore readings past this
  double SENSOR_STD_FRAC;      // std-dev as a fraction of the reading
  double SENSOR_STD_FLOOR;     // inches; smallest std-dev allowed
  double DISTANCE_WEIGHT;      // how strongly distance sensors vote

  // ---- robustness ----
  double HEAVY_TAIL_FLOOR;     // min per-sensor vote (one bad reading can't zero a particle)
  double ROUGHEN_STD;          // inches of jitter after resampling (keeps diversity)
  double INJECT_FRAC;          // fraction randomized each step (0 = off; ~0.02 for kidnap recovery)

  // ---- integration ----
  double MCL_WEIGHT;           // 0..1, how hard MCL pulls chassis toward its estimate
  double SEED_STD;             // inches of initial scatter around the start pose
  int    TASK_DELAY_MS;        // mcl task loop delay (~50 Hz = 20 ms)

  // ---- per-sensor mounts (index matches DistSensor1..4) ----
  double SENSOR_FORWARD[4];    // inches ahead of tracking center (back = negative)
  double SENSOR_SIDEWAYS[4];   // inches right of tracking center (left = negative)
  double SENSOR_FACE_ANGLE[4];  // facing vs robot front (0 front, 90 right, 180 back, 270 left)
  double SENSOR_SCALE[4];      // per-sensor scale fix (~1.00-1.03; sensors read a bit high)

  // ---- slip physics (the sideslip FORCE knob), shared with boomerang ----
  double SIDE_PUSH_FORCE;      // NEWTONS: sideways force to make the wheels slide (measured)
  double ROBOT_WEIGHT;         // kg: the robot's mass (measured on a scale)

  // Max sideways acceleration before sliding, straight from Newton's 2nd law: a = F / m.
  // SIDE_PUSH_FORCE is in N, ROBOT_WEIGHT in kg -> a in m/s^2; * 39.3701 converts to in/s^2.
  double maxLateralAccel() const {
    return (SIDE_PUSH_FORCE / ROBOT_WEIGHT) * 39.3701;
  }
};

// The one global config instance. Tune its values in mclConfig.cpp.
extern MclConfig MCLConfig;

// Sideways pull on the robot right now: centripetal acceleration a = v^2 / r.
// Free helper, shared by boomerang (to LIMIT speed) and MCL (to MODEL slip).
// Straight driving -> arcRadius huge -> a ~ 0.  Tight fast turn -> small radius -> big a.
inline double centripetalAcceleration(double velocity, double arcRadius) {
  double radius = fabs(arcRadius);
  if (radius < 1.0) {
    radius = 1.0;            // guard a near-zero radius from blowing up the division
  }
  return (velocity * velocity) / radius;
}
