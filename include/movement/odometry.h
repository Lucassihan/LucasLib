#pragma once
#include "vex.h"

using namespace vex;

enum DriveEncoderPosition {
  front = 0,
  middle = 1,
  back = 2
};

struct OdometryConfig {
  inertial* imu1               = nullptr;
  inertial* imu2               = nullptr;  // omit for single-IMU
  rotation* yTracker           = nullptr;  // omit to use motor encoders for Y
  rotation* xTracker           = nullptr;  // omit for no X tracking
  double    trackwidth         = 0;
  double    imu1_multiplier    = 1.0;
  double    imu2_multiplier    = 1.0;
  double    yTrackerDiameter   = 0;
  double    xTrackerDiameter   = 0;
  double    yTrackerOffset     = 0;
  double    xTrackerOffset     = 0;
  double    driveWheelDiameter = 0;        // used for motor-encoder Y and heading tiebreaker
  double    driveGearRatio     = 1.0;      // wheel rotations per motor rotation
  DriveEncoderPosition driveEncoder = front;  // which motor pair to use for motor-encoder Y tracking

  double    sensor1Offset      = 0;        // physical distance from robot tracking center to each distance sensor face
  double    sensor2Offset      = 0;
  double    sensor3Offset      = 0;
  double    sensor4Offset      = 0;
};

class Odometry {
public:
  double x = 0;   // desired tracking point (what movements / MCL read), derived by applyPoseOffsets
  double y = 0;
  double h = 0;

  // Raw integrated pose = the intersection of the two tracker axes, before mount offsets.
  // trackY/trackX accumulate into these; applyPoseOffsets derives (x, y) from them + heading.
  double trueX = 0;
  double trueY = 0;

  // Forward velocity in inches/sec, refreshed on each Y accumulation from encoder deltas.
  double velocity = 0;

  //Cumulative wheel-rolled distance in robot's local frame, since last clearTrackedInches() call.
  //Useful for movements that want "drive N inches forward" rather than "drive to point (x, y)".
  double yInchesTracked = 0;
  double xInchesTracked = 0;

  // Per-axis noise floors in RAW sensor degrees (compared before scaling to inches): a raw
  // delta smaller than this is below the sensor's resolution and treated as noise. Set once
  // in the constructor from the sensor type (rotation sensor vs drive-motor cartridge).
  double yNoiseThreshold = 0;
  double xNoiseThreshold = 0;

  // Tracker mount offsets, copied from config so odometryTask can hand them to applyPoseOffsets.
  // xTrackerOffset = X tracker's forward (+Y) distance from the desired tracking point;
  // yTrackerOffset = Y tracker's lateral (+X) distance. Both 0 = track the axis intersection.
  double xTrackerOffset = 0;
  double yTrackerOffset = 0;

  // --- MCL integration ---
  // trackHeading/trackY/trackX accumulate their per-tick GLOBAL displacement here; the MCL
  // task consumes it (as the predict increment) and blends a correction back into x/y.
  // Guarded by mclMutex.
  mutex  mclMutex;
  double mclAccumDX = 0;
  double mclAccumDY = 0;

  Odometry(OdometryConfig config);

  double getTrackwidth() { return cfg.trackwidth; }  // for trace export

  void setPos(double newX, double newY, double newH);

  // --- per-tick tracking, driven by odometryTask (HongKou straight-line model) ---
  // trackHeading: fuse the IMU(s) and advance heading. Runs every tick (never noise-gated).
  // yRawDeltaDeg / xRawDeltaDeg: raw rotation since the last accumulation, in sensor
  //   degrees, BEFORE any scaling -- this is what odometryTask gates against the noise floor.
  // trackY / trackX: scale that raw delta to inches and integrate it into the pose. The point
  //   they track is the intersection of the two tracker axes (no offsets here).
  // applyPoseOffsets: translate that intersection point to the desired tracking point using the
  //   mount offsets + heading. Call after trackY/trackX have accumulated.
  void   trackHeading();
  double yRawDeltaDeg();
  double xRawDeltaDeg();
  void   trackY(double rawDeltaDeg);
  void   trackX(double rawDeltaDeg);
  void   applyPoseOffsets(double offsetX, double offsetY);

  void clearTrackedInches();
  void readWall(int wall, int sensor, bool bypassSettleDelay = false);

  double wrapAngle(double angle);
  double distanceTo(double targetX, double targetY, bool backward = false);

  //Signed radius (inches) of the arc the robot traced over the last Y accumulation: r = ds/dtheta.
  //Sign follows heading change (CW+). Near-straight motion returns a large sentinel (~1e9).
  //Used by boomerang slip-limiting (centripetal: v_max = sqrt(grip * |r|)).
  double travelArcRadius();
  double forwardProgress(double startX, double startY, double headingDeg);
  double wrappedHeadingError(double targetH, bool backward = false);
  double positiveHeadingError(double targetH, bool backward = false);
  double negativeHeadingError(double targetH, bool backward = false);
  double angleToPoint(double targetX, double targetY, bool backward = false);
  double wrappedPointHeadingError(double targetX, double targetY, bool backward = false);
  double positivePointHeadingError(double targetX, double targetY, bool backward = false);
  double negativePointHeadingError(double targetX, double targetY, bool backward = false);

  bool noEncodersUpdated;

private:
  OdometryConfig cfg;

  bool hasYTracker;
  bool hasXTracker;
  bool hasSecondIMU;

  motor* leftEncoder;
  motor* rightEncoder;

  bool imu1Dead  = false;
  bool imu2Dead  = false;

  double lastIMU1     = 0;
  double lastIMU2     = 0;
  double lastYPos     = 0;   // Y accumulation baseline (raw deg); advances only when Y accumulates
  double lastXPos     = 0;   // X accumulation baseline (raw deg); advances only when X accumulates
  double lastLeftPos  = 0;
  double lastRightPos = 0;

  // Heading change (deg) banked since the last Y accumulation. Half of it re-centers the Y step
  // on its window midpoint (second-order placement); the full swing feeds velocity and arc
  // radius over that same span as the forward distance they are paired with.
  double dhAccumDeg = 0;
  // Same idea for the X window (zeroed by trackX, not trackY), so each axis places its step at
  // its own midpoint heading rather than sharing Y's accumulator.
  double dhAccumDegX = 0;

  double lastTrackTime = 0;

  //Cached arc radius (inches) from the most recent Y accumulation. See travelArcRadius().
  double lastArcRadius = 1e9;

  double resolveHeading(double dIMU1, double dIMU2, double dLDeg, double dRDeg);
  double motorDegsToInches(double deg);
  double trackerDegsToInches(double deg, double diameter);
  double motorTickDegrees();   // drive-encoder resolution (deg/tick) from the gear cartridge
  double yRawPosition();       // current raw Y reading (tracker deg, or avg drive-encoder deg)
  double xRawPosition();       // current raw X reading (tracker deg, or 0 with no X tracker)
};

extern Odometry chassis;

int odometryTask();