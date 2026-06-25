#include "mcl/mclConfig.h"

// ============================================================================
// mclConfig.cpp -- THE NUMBERS. Tune the robot here. One global struct instance.
// (Structural constants NUM_PARTICLES / FIELD_HALF live in mclConfig.h.)
// ============================================================================

MclConfig MCLConfig = {
  // ---- predict noise ----
  .FORWARD_NOISE       = 0.08,   // along the heading (we have a forward tracker)
  .SIDEWAYS_NOISE      = 0.20,   // base sideways noise (NO lateral tracker)
  .SLIP_GAIN           = 0.003,  // extra sideways noise once the robot is sliding

  // ---- update gating ----
  .MIN_UPDATE_DIST     = 2.0,    // inches
  .MAX_NO_UPDATE_TIME = 2.0,    // seconds

  // ---- distance sensor model ----
  .SENSOR_MAX_RANGE    = 78.0,   // inches (~2 m practical cap)
  .SENSOR_STD_FRAC     = 0.025,  // 2.5% of the reading (datasheet-derived)
  .SENSOR_STD_FLOOR    = 0.5,    // inches
  .DISTANCE_WEIGHT     = 1.0,

  // ---- robustness (these turned out to be REQUIRED, see the docs) ----
  .HEAVY_TAIL_FLOOR    = 0.05,   // a wildly-wrong sensor still leaves 5% weight (no zeroing)
  .ROUGHEN_STD         = 0.4,    // inches of post-resample jitter (prevents collapse)
  .INJECT_FRAC         = 0.0,    // OFF: random injection biases unobserved axes toward field-center.
                                 // raise to ~0.02 ONLY if you need bump/kidnap recovery.

  // ---- integration ----
  .MCL_WEIGHT          = 0.35,   // 7700R value; 1.0 = snap straight to MCL
  .SEED_STD            = 1.5,    // inches of initial scatter
  .TASK_DELAY_MS       = 20,     // ~50 Hz

  // ====================== MEASURE THESE ON YOUR ROBOT ======================
  // Index order matches DistSensor1..4 (coreConfigs.cpp).
  //   FORWARD  = inches AHEAD of tracking center  (a back-facing sensor is negative)
  //   SIDEWAYS = inches to the RIGHT of center     (a left sensor is negative)
  //   ANGLE    = which way it points vs robot front (0 front, 90 right, 180 back, 270 left)
  //   SCALE    = face a wall at known distances, set so the reading matches truth
  // Placeholders below assume 4 corner sensors; REPLACE with real measurements.
  .SENSOR_FORWARD   = {  6.0,   6.0,  -6.0,  -6.0 },
  .SENSOR_SIDEWAYS  = {  5.0,  -5.0,  -5.0,   5.0 },
  .SENSOR_FACE_ANGLE = {  0.0,   0.0, 180.0, 180.0 },
  .SENSOR_SCALE     = {  1.0,   1.0,   1.0,   1.0 },

  // ---- slip physics: the sideslip FORCE knob ----
  .SIDE_PUSH_FORCE  = 30.0,      // NEWTONS: sideways force to break the wheels loose.
                                 //   force gauge reads N directly; a kg luggage-scale
                                 //   reading converts as  N = kg x 9.81.
  .ROBOT_WEIGHT     = 5.0,       // kg: the robot's mass (on a scale)
};
