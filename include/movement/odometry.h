#pragma once
#include "vex.h"
#include "util.h"

using namespace vex;

enum DriveEncoderPosition {
  front = 0,
  middle = 1,
  back = 2
};

// Where a wall-reset distance sensor is mounted, as axis distances from the tracking center in the robot's LOCAL frame.
struct DistanceSensorOffset {
  double y = 0;   // + = sensor face sits in front of the tracking center
  double x = 0;   // + = sensor face sits to the right of the tracking center
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

  DistanceSensorOffset sensor1Offset;
  DistanceSensorOffset sensor2Offset;
  DistanceSensorOffset sensor3Offset;
  DistanceSensorOffset sensor4Offset;
};

class Odometry {
public:
  double x = 0;   // desired tracking point (what movements read), derived by applyPoseOffsets
  double y = 0;
  double h = 0;

  // Raw integrated pose: the intersection of the two tracker axes, before mount offsets.
  double trueX = 0;
  double trueY = 0;

  double velocity = 0;   // in/s forward, refreshed on each Y accumulation

  // Wheel-rolled distance in the robot's local frame since the last clearTrackedInches().
  double yInchesTracked = 0;
  double xInchesTracked = 0;

  // Per-axis noise floor in RAW sensor degrees; a smaller delta is below the sensor's resolution. Set in the constructor from the sensor type.
  double yNoiseThreshold = 0;
  double xNoiseThreshold = 0;

  // Tracker mount offsets, copied from config so odometryTask can hand them to applyPoseOffsets. Both 0 = track the axis intersection.
  double xTrackerOffset = 0;   // X tracker's forward (+Y) distance from the desired tracking point
  double yTrackerOffset = 0;   // Y tracker's lateral (+X) distance

  Odometry(OdometryConfig config);

  double getTrackwidth() { return odomConfigs.trackwidth; }  // for trace export

  void setPos(double newX, double newY, double newH);

  // --- per-tick tracking, driven by odometryTask (HongKou straight-line model) ---
  void   trackHeading();               // fuse the IMU(s) and advance heading; runs every tick, never noise-gated
  double yRawDeltaDeg();               // raw rotation since the last accumulation, BEFORE scaling — what odometryTask gates
  double xRawDeltaDeg();
  void   trackY(double rawDeltaDeg);   // scale to inches and integrate into the axis-intersection point
  void   trackX(double rawDeltaDeg);
  void   applyPoseOffsets(double offsetX, double offsetY);  // intersection point → desired tracking point; call after trackY/trackX
  void updateVelocity();

  void clearTrackedInches();
  void readWall(int wall, int sensor, bool bypassSettleDelay = false);

  double wrapAngle(double angle);
  double distanceTo(double targetX, double targetY, bool backward = false);

  // Signed radius (in) of the arc traced over the last Y accumulation, r = ds/dtheta. Sign follows heading change (CW+); near-straight returns ~1e9.
  double travelArcRadius();
  double forwardProgress(double startX, double startY, double headingDeg);
  // No `backward` on the heading errors: a heading is always where the NOSE points. The 180 a tail-first approach needs belongs to angleToPoint.
  double wrappedHeadingError(double targetH);
  double positiveHeadingError(double targetH);
  double negativeHeadingError(double targetH);
  double angleToPoint(double targetX, double targetY, bool backward = false);
  double wrappedPointHeadingError(double targetX, double targetY, bool backward = false);
  double positivePointHeadingError(double targetX, double targetY, bool backward = false);
  double negativePointHeadingError(double targetX, double targetY, bool backward = false);

  //Velocities, in inches. Smoothed — this is what the velocity layer reads through setSource.
  double v;
  double ω;

  // The same two before smoothing. Nothing steers off these; they are what a trace should carry if
  // the question is how noisy the measurement itself is.
  double vRaw;
  double ωRaw;

  int nEncodersUpdated;
  bool hasYTracker;
  bool hasXTracker;

private:
  OdometryConfig odomConfigs;

  bool hasSecondIMU;
  bool justResetPos;

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
  double lastVRefX = 0;
  double lastVRefY = 0;
  double lastVRefH = 0;

  VelocityHistory vHistory;
  VelocityHistory ωHistory;

  // Heading change (deg) banked since the last Y accumulation. Half re-centers the Y step on its window midpoint; the full swing feeds velocity and arc radius.
  double dhAccumDeg = 0;
  double dhAccumDegX = 0;   // same for the X window, zeroed by trackX, so each axis places its step at its own midpoint heading

  uint64_t lastTrackTime = 0;

  double lastArcRadius = 1e9;   // cached from the most recent Y accumulation

  double resolveHeading(double dIMU1, double dIMU2, double dLDeg, double dRDeg);
  double motorDegsToInches(double deg);
  double trackerDegsToInches(double deg, double diameter);
  double motorTickDegrees();   // drive-encoder resolution (deg/tick) from the gear cartridge
  double yRawPosition();       // current raw Y reading (tracker deg, or avg drive-encoder deg)
  double xRawPosition();       // current raw X reading (tracker deg, or 0 with no X tracker)
};

extern Odometry chassis;

int odometryTask();
