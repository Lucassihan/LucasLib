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
  double x = 0;
  double y = 0;
  double h = 0;

  // Forward velocity in inches/sec, computed each track() cycle from encoder deltas.
  double velocity = 0;

  //Cumulative wheel-rolled distance in robot's local frame, since last clearTrackedInches() call.
  //Useful for movements that want "drive N inches forward" rather than "drive to point (x, y)".
  double yInchesTracked = 0;
  double xInchesTracked = 0;

  Odometry(OdometryConfig config);

  void setPos(double newX, double newY, double newH);
  void track();
  void clearTrackedInches();
  void readWall(int wall, int sensor, bool bypassSettleDelay = false);

  double wrapAngle(double angle);
  double distanceTo(double targetX, double targetY, bool backward = false);

  //Signed radius (inches) of the arc the robot traced on the last track() cycle: r = ds/dtheta.
  //Sign follows heading change (CW+). Near-straight motion returns a large sentinel (~1e9).
  //Used by boomerang slip-limiting (centripetal: v_max = sqrt(grip * |r|)).
  double travelArcRadius();
  double forwardProgress(double startX, double startY, double headingDeg);
  double wrappedHeadingError(double targetH, bool backward = false);
  double positiveHeadingError(double targetH, bool backward = false);
  double negativeHeadingError(double targetH, bool backward = false);
  double wrappedPointHeadingError(double targetX, double targetY, bool backward = false);
  double positivePointHeadingError(double targetX, double targetY, bool backward = false);
  double negativePointHeadingError(double targetX, double targetY, bool backward = false);

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
  double lastYPos     = 0;
  double lastXPos     = 0;
  double lastLeftPos  = 0;
  double lastRightPos = 0;

  uint64_t lastTrackTime = 0;

  //Cached arc radius (inches) from the most recent track() cycle. See travelArcRadius().
  double lastArcRadius = 1e9;

  double resolveHeading(double dIMU1, double dIMU2, double dLDeg, double dRDeg);
  double motorDegsToInches(double deg);
  double trackerDegsToInches(double deg, double diameter);
};

extern Odometry chassis;

int odometryTask();