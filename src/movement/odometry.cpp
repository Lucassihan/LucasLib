#include "movement/odometry.h"
#include "coreConfigs.h"
#include "util.h"
#include "debugger/debugger.h"

#include <cmath>

static motor* pickMotor(DriveEncoderPosition pos, motor& f, motor& m, motor& b) {
  if (pos == middle) {
    return &m;
  }
  if (pos == back) {
    return &b;
  }
  return &f;
}

//constructor
Odometry::Odometry(OdometryConfig config) : cfg(config) {
  hasYTracker  = (cfg.yTracker  != nullptr);
  hasXTracker  = (cfg.xTracker  != nullptr);
  hasSecondIMU = (cfg.imu2      != nullptr);

  leftEncoder  = pickMotor(cfg.driveEncoder, fl, ml, bl);
  rightEncoder = pickMotor(cfg.driveEncoder, fr, mr, br);

  lastIMU1     = cfg.imu1->rotation(rotationUnits::deg);
  lastLeftPos  = leftEncoder->position(rotationUnits::deg);
  lastRightPos = rightEncoder->position(rotationUnits::deg);

  if (hasSecondIMU) {
    lastIMU2 = cfg.imu2->rotation(rotationUnits::deg);
  }
  if (hasYTracker) {
    lastYPos = cfg.yTracker->position(rotationUnits::deg);
  }
  if (hasXTracker) {
    lastXPos = cfg.xTracker->position(rotationUnits::deg);
  }
}

//setPos
void Odometry::setPos(double newX, double newY, double newH) {
  x = newX;
  y = newY;
  h = newH;
}

//conversion helpers
double Odometry::motorDegsToInches(double deg) {
  return deg * (cfg.driveWheelDiameter * M_PI * cfg.driveGearRatio) / 360.0;
}

double Odometry::trackerDegsToInches(double deg, double diameter) {
  return deg * (diameter * M_PI) / 360.0;
}

//heading resolution
double Odometry::resolveHeading(double dIMU1, double dIMU2, double dLDeg, double dRDeg) {
  double scaled1 = dIMU1 * cfg.imu1_multiplier;
  double scaled2 = dIMU2 * cfg.imu2_multiplier;

  if (!hasSecondIMU || imu2Dead) {
    return scaled1;
  }

  if (imu1Dead) {
    return scaled2;
  }

  if (fabs(scaled1 - scaled2) >= 5.0) {
    double dhWheelsDeg = (motorDegsToInches(dRDeg) - motorDegsToInches(dLDeg))
                         / cfg.trackwidth * (180.0 / M_PI);

    if (fabs(scaled1 - dhWheelsDeg) <= fabs(scaled2 - dhWheelsDeg)) {
      imu2Dead = true;
      return scaled1;
    } else {
      imu1Dead = true;
      return scaled2;
    }
  }

  return (scaled1 + scaled2) / 2.0;
}

//track
void Odometry::track() {
  //read all sensors
  double curIMU1  = cfg.imu1->rotation(rotationUnits::deg);
  double curIMU2  = hasSecondIMU ? cfg.imu2->rotation(rotationUnits::deg)    : 0;
  double curYPos  = hasYTracker  ? cfg.yTracker->position(rotationUnits::deg) : 0;
  double curXPos  = hasXTracker  ? cfg.xTracker->position(rotationUnits::deg) : 0;
  double curLeft  = leftEncoder->position(rotationUnits::deg);
  double curRight = rightEncoder->position(rotationUnits::deg);

  //compute deltas
  double dIMU1  = curIMU1  - lastIMU1;
  double dIMU2  = curIMU2  - lastIMU2;
  double dLDeg  = curLeft  - lastLeftPos;
  double dRDeg  = curRight - lastRightPos;

  //heading
  double dhDeg = resolveHeading(dIMU1, dIMU2, dLDeg, dRDeg);
  double dhRad = dhDeg * (M_PI / 180.0);

  //Y displacement in inches
  double dYInches;
  if (hasYTracker) {
    dYInches = trackerDegsToInches(curYPos - lastYPos, cfg.yTrackerDiameter);
  } else {
    dYInches = motorDegsToInches((dLDeg + dRDeg) / 2.0);
  }

  //X displacement in inches
  double dXInches = 0;
  if (hasXTracker) {
    dXInches = trackerDegsToInches(curXPos - lastXPos, cfg.xTrackerDiameter);
  }

  //arc-length position update
  double localDX, localDY;

  if (fabs(dhRad) < 1e-6) {
    localDX = dXInches;
    localDY = dYInches;
  } else {
    double chordFactor = 2.0 * sin(dhRad / 2.0);
    localDY = chordFactor * (dYInches / dhRad + (hasYTracker  ? cfg.yTrackerOffset : 0));
    localDX = chordFactor * (dXInches / dhRad + (hasXTracker  ? cfg.xTrackerOffset : 0));
  }

  //Accumulate offset-corrected local-frame motion for driveForward-style movements
  yInchesTracked += localDY;
  xInchesTracked += localDX;

  //Instantaneous arc radius this cycle: r = ds/dtheta (forward roll over heading change).
  //Near-zero heading change means (near-)straight travel -> large sentinel radius.
  if (fabs(dhRad) < 1e-6) {
    lastArcRadius = 1e9;
  } else {
    lastArcRadius = dYInches / dhRad;
  }

  // Forward velocity in inches/sec
  uint64_t now = vex::timer::systemHighResolution();
  double dtSec = (now - lastTrackTime) / 1000000.0;
  if (lastTrackTime > 0 && dtSec > 0) {
    velocity = localDY / dtSec;
  }
  lastTrackTime = now;

  //rotate local to global frame (clockwise-positive heading, 0 = north)
  double avgHRad = h * (M_PI / 180.0) + dhRad / 2.0;
  double gdx = localDY * sin(avgHRad) + localDX * cos(avgHRad);
  double gdy = localDY * cos(avgHRad) - localDX * sin(avgHRad);

  mclMutex.lock();
  x += gdx;
  y += gdy;
  mclAccumDX += gdx;   // MCL task consumes this as its predict increment
  mclAccumDY += gdy;
  mclMutex.unlock();

  h += dhDeg;

  //update last values
  lastIMU1     = curIMU1;
  lastLeftPos  = curLeft;
  lastRightPos = curRight;

  if (hasSecondIMU) {
    lastIMU2 = curIMU2;
  }
  if (hasYTracker) {
    lastYPos = curYPos;
  }
  if (hasXTracker) {
    lastXPos = curXPos;
  }
}

//readWall
void Odometry::readWall(int wall, int sensor, bool bypassSettleDelay) {
  double distanceReading = 0;
  double sensorOffset    = 0;

  if (sensor == 1) {
    distanceReading = DistSensor1.objectDistance(inches);
    sensorOffset    = cfg.sensor1Offset;
  } else if (sensor == 2) {
    distanceReading = DistSensor2.objectDistance(inches);
    sensorOffset    = cfg.sensor2Offset;
  } else if (sensor == 3) {
    distanceReading = DistSensor3.objectDistance(inches);
    sensorOffset    = cfg.sensor3Offset;
  } else if (sensor == 4) {
    distanceReading = DistSensor4.objectDistance(inches);
    sensorOffset    = cfg.sensor4Offset;
  }

  double relativeOffset = sensorOffset - distanceReading;

  // walls 1 & 2 are positive edges (+48), walls 3 & 4 are negative edges (-48)
  double sign;
  if (wall <= 2) {
    sign = 1;
  } else {
    sign = -1;
  }

  double newPos = sign * (48.0 + relativeOffset);

  double preX = x, preY = y, preH = h;  // pose before the correction (for the debugger)

  // walls 1 & 3 set X, walls 2 & 4 set Y
  //    2
  // 3     1
  //    4
  if (wall == 1 || wall == 3) {
    setPos(newPos, y, h);
  } else if (wall == 2 || wall == 4) {
    setPos(x, newPos, h);
  }

  debugger::readWall(wall, sensor, preX, preY, preH, x, y, h);
}

//helper math
//Absolute field bearing (deg, 0 = +Y, CW+) from the robot to (targetX, targetY),
//flipped 180 if backward. This is the raw turn TARGET; wrappedPointHeadingError is
//just this minus current heading, wrapped. Kept separate so callers can offset the
//target (e.g. turnOvershoot) BEFORE wrapping — adding to an already-wrapped error
//would turn the long way around near the ±180 boundary.
double Odometry::angleToPoint(double targetX, double targetY, bool backward) {
  double targetH = atan2(targetX - x, targetY - y) * (180.0 / M_PI);
  if (backward) {
    targetH += 180;
  }
  return targetH;
}

double Odometry::wrappedPointHeadingError(double targetX, double targetY, bool backward) {
  return wrapAngle(angleToPoint(targetX, targetY, backward) - h);
}

double Odometry::positivePointHeadingError(double targetX, double targetY, bool backward) {
  double error = wrappedPointHeadingError(targetX, targetY, backward);
  if (error < 0) {
    error += 360.0;
  }
  return error;
}

double Odometry::negativePointHeadingError(double targetX, double targetY, bool backward) {
  double error = wrappedPointHeadingError(targetX, targetY, backward);
  if (error > 0) {
    error -= 360.0;
  }
  return error;
}

double Odometry::wrappedHeadingError(double targetH, bool backward) {
  if (backward) {
    targetH += 180;
  }
  return wrapAngle(targetH - h);
}

double Odometry::positiveHeadingError(double targetH, bool backward) {
  double error = wrappedHeadingError(targetH, backward);
  if (error < 0) {
    error += 360.0;
  }
  return error;
}

double Odometry::negativeHeadingError(double targetH, bool backward) {
  double error = wrappedHeadingError(targetH, backward);
  if (error > 0) {
    error -= 360.0;
  }
  return error;
}

double Odometry::wrapAngle(double angle) {
  while (angle > 180.0) {
    angle -= 360.0;
  }
  while (angle < -180.0) {
    angle += 360.0;
  }
  return angle;
}

double Odometry::distanceTo(double targetX, double targetY, bool backward) {
  double dx = targetX - x;
  double dy = targetY - y;
  double dist = sqrt(dx * dx + dy * dy);
  if (backward) {
    dist = -dist;
  }
  return dist;
}

double Odometry::travelArcRadius() {
  return lastArcRadius;
}

//Signed forward displacement from a captured start point, projected onto the
//given heading axis (0 = north, clockwise-positive). The forward unit vector at
//heading h is (sin h, cos h), matching the position update in track().
//Positive = moved forward along the axis, negative = moved backward.
//This is the pose-based replacement for the wheel-encoder yInchesTracked
//odometer: it reads straight off (x, y), so movement error follows the fused
//(MCL) pose rather than raw encoders, and a pose correction just changes the
//current reading instead of being permanently accumulated.
double Odometry::forwardProgress(double startX, double startY, double headingDeg) {
  double axisRad = headingDeg * (M_PI / 180.0);
  return (x - startX) * sin(axisRad) + (y - startY) * cos(axisRad);
}

//Resets the cumulative tracked-inches counters. Called by clearAllValues() at the start of each movement.
void Odometry::clearTrackedInches() {
  yInchesTracked = 0;
  xInchesTracked = 0;
}

int odometryTask() {
  while (true) {
    chassis.track();
    delay(5);
  }
}