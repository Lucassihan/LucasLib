#include "movement/odometry.h"
#include "coreConfigs.h"
#include "util.h"
#include "debugger/debugger.h"

#include <cmath>

static motor* pickMotor(DriveEncoderPosition pos, motor& f, motor& m, motor& b) {
  if (pos == middle) return &m;
  if (pos == back)   return &b;
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

  // Seed the Y/X accumulation baselines (scalar raw positions: tracker degrees, or the
  // average of the two drive encoders when there is no Y tracker).
  lastYPos = yRawPosition();
  lastXPos = xRawPosition();

  // Per-axis noise floors, in raw sensor degrees (compared BEFORE scaling to inches).
  // A V5 rotation sensor resolves 360/4096 deg/tick (~0.088); a drive encoder resolves
  // 360/ticksPerRev deg/tick, where ticksPerRev is fixed by its gear cartridge.
  const double kRotationSensorDeg = 360.0 / 4096.0;  // ~0.0879 deg/tick

  yNoiseThreshold = hasYTracker ? kRotationSensorDeg : motorTickDegrees();
  // X is always a rotation sensor when present; with no X tracker rawX stays 0, which this
  // floor filters out, so trackX is never called.
  xNoiseThreshold = kRotationSensorDeg;

  // Expose the configured mount offsets, then seed the intersection point consistent with the
  // start pose (x = y = h = 0): trueX/trueY = (x, y) + the offset vector rotated by heading.
  xTrackerOffset = cfg.xTrackerOffset;
  yTrackerOffset = cfg.yTrackerOffset;

  double hr = TO_RAD(h);
  trueX = x + ( yTrackerOffset * cos(hr) + xTrackerOffset * sin(hr));
  trueY = y + (-yTrackerOffset * sin(hr) + xTrackerOffset * cos(hr));
}

//setPos
void Odometry::setPos(double newX, double newY, double newH) {
  x = newX;
  y = newY;
  h = newH;

  // x/y is the desired point; back-compute the intersection point that trackX/trackY integrate,
  // so the next applyPoseOffsets reproduces this pose exactly. (trueX/trueY = desired + R(h)*offset.)
  double hr = TO_RAD(newH);
  trueX = newX + ( yTrackerOffset * cos(hr) + xTrackerOffset * sin(hr));
  trueY = newY + (-yTrackerOffset * sin(hr) + xTrackerOffset * cos(hr));
}

//conversion helpers
double Odometry::motorDegsToInches(double deg) {
  return deg * (cfg.driveWheelDiameter * M_PI * cfg.driveGearRatio) / 360.0;
}

double Odometry::trackerDegsToInches(double deg, double diameter) {
  return deg * (diameter * M_PI) / 360.0;
}

//Drive-encoder angular resolution (degrees per tick), set by the gear cartridge:
//  36:1 -> 1800 ticks/rev, 18:1 -> 900, 6:1 -> 300.  resolution = 360 / ticksPerRev.
double Odometry::motorTickDegrees() {
  double ticksPerRev = 900.0;  // 18:1 (green) default
  gearSetting cartridge = leftEncoder->getMotorCartridge();
  if (cartridge == ratio36_1) ticksPerRev = 1800.0;
  else if (cartridge == ratio6_1) ticksPerRev = 300.0;
  return 360.0 / ticksPerRev;
}

//Raw Y reading (degrees), BEFORE scaling: the Y tracker if present, otherwise the average
//of the two drive encoders (their common-mode motion is forward travel).
double Odometry::yRawPosition() {
  if (hasYTracker) return cfg.yTracker->position(rotationUnits::deg);
  return (leftEncoder->position(rotationUnits::deg) + rightEncoder->position(rotationUnits::deg)) / 2.0;
}

//Raw X reading (degrees), BEFORE scaling: the X tracker if present, otherwise 0 (no lateral tracking).
double Odometry::xRawPosition() {
  return hasXTracker ? cfg.xTracker->position(rotationUnits::deg) : 0;
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

//trackHeading -- fuse the IMU(s), advance heading, apply the off-center offset translation.
//Runs every tick (heading is read from the IMU, never noise-gated by the encoders).
void Odometry::trackHeading() {
  double curIMU1 = cfg.imu1->rotation(rotationUnits::deg);
  double curIMU2 = hasSecondIMU ? cfg.imu2->rotation(rotationUnits::deg) : 0;
  double curLeft  = leftEncoder->position(rotationUnits::deg);
  double curRight = rightEncoder->position(rotationUnits::deg);

  double dIMU1 = curIMU1 - lastIMU1;
  double dIMU2 = curIMU2 - lastIMU2;
  double dLDeg = curLeft  - lastLeftPos;
  double dRDeg = curRight - lastRightPos;

  double dhDeg = resolveHeading(dIMU1, dIMU2, dLDeg, dRDeg);

  h += dhDeg;
  dhAccumDeg  += dhDeg;  // bank heading change for the next Y accumulation's midpoint + arc radius
  dhAccumDegX += dhDeg;  // parallel bank so trackX can place its step at the X window midpoint

  lastIMU1     = curIMU1;
  lastLeftPos  = curLeft;
  lastRightPos = curRight;
  if (hasSecondIMU) lastIMU2 = curIMU2;
}

//applyPoseOffsets -- derive the desired tracking point (x, y) from the integrated intersection
//point (trueX, trueY). The intersection sits a fixed vector off the desired point in the robot
//frame, so the desired point is the intersection minus that vector rotated into the field:
//  x = trueX - (offsetY*cos h + offsetX*sin h)
//  y = trueY - (-offsetY*sin h + offsetX*cos h)
//With both offsets 0 this collapses to x = trueX, y = trueY. It's a pure function of
//(trueX, trueY, h), so it always lands exactly right for the current state -- no accumulated
//offset state to drift, and a skipped (noise) tick just means x/y refreshes on the next call.
//  offsetX = X tracker's forward (+Y) offset; offsetY = Y tracker's lateral (+X) offset.
void Odometry::applyPoseOffsets(double offsetX, double offsetY) {
  double hr = TO_RAD(h);
  double gx =  offsetY * cos(hr) + offsetX * sin(hr);
  double gy = -offsetY * sin(hr) + offsetX * cos(hr);

  mclMutex.lock();
  x = trueX - gx;
  y = trueY - gy;
  mclMutex.unlock();
}

//Raw rotation (degrees) accumulated on each axis since the last accumulation. odometryTask
//gates these against the noise floor BEFORE any scaling. No side effects -- the baseline only
//advances when trackY/trackX actually consume the delta, so sub-threshold motion stays banked.
double Odometry::yRawDeltaDeg() {
  return yRawPosition() - lastYPos;
}

double Odometry::xRawDeltaDeg() {
  return xRawPosition() - lastXPos;
}

//trackY -- scale the (already past-threshold) raw Y rotation to inches and integrate it into the
//global pose. The step is placed along the MIDPOINT heading of the window it accrued over, not the
//end heading: h is the latest (end-of-window) heading and dhAccumDeg is the swing across the same
//window, so h - dhAccumDeg/2 is the average. This is second-order accurate on a simultaneous
//move-and-turn (and on a banked chunk released after several ticks) instead of biasing toward the
//heading the robot finished at. 0 = north, CW+.
void Odometry::trackY(double rawDeltaDeg) {
  double dYInches = hasYTracker ? trackerDegsToInches(rawDeltaDeg, cfg.yTrackerDiameter)
                                : motorDegsToInches(rawDeltaDeg);

  double headingRad = TO_RAD(h - dhAccumDeg / 2.0);  // window midpoint, not end
  double gdx = dYInches * sin(headingRad);
  double gdy = dYInches * cos(headingRad);

  mclMutex.lock();
  trueX += gdx;
  trueY += gdy;
  mclAccumDX += gdx;
  mclAccumDY += gdy;
  mclMutex.unlock();

  yInchesTracked += dYInches;

  // Velocity and arc radius over the interval since the last Y accumulation, so forward
  // travel and heading change span the same window. Held (not zeroed) between accumulations.
  double now = Brain.timer(msec);
  double dtSec = (now - lastTrackTime) / 1000.0;
  if (lastTrackTime > 0 && dtSec > 0) velocity = dYInches / dtSec;
  lastTrackTime = now;

  double dhRad = TO_RAD(dhAccumDeg);
  lastArcRadius = (fabs(dhRad) < 1e-6) ? 1e9 : dYInches / dhRad;  // near-straight -> large sentinel
  dhAccumDeg = 0;

  // Advance the baseline by exactly what we consumed (sub-threshold remainder stays banked).
  lastYPos += rawDeltaDeg;
}

//trackX -- same as trackY for the lateral axis, placed along the MIDPOINT heading of its own
//window (dhAccumDegX is the heading swing banked since the last X accumulation). Local +X maps to
//global (cos h, -sin h). Only reached when an X tracker exists (otherwise rawDeltaDeg stays 0 and
//is filtered).
void Odometry::trackX(double rawDeltaDeg) {
  double dXInches = trackerDegsToInches(rawDeltaDeg, cfg.xTrackerDiameter);

  double headingRad = TO_RAD(h - dhAccumDegX / 2.0);  // window midpoint, not end
  double gdx =  dXInches * cos(headingRad);
  double gdy = -dXInches * sin(headingRad);

  mclMutex.lock();
  trueX += gdx;
  trueY += gdy;
  mclAccumDX += gdx;
  mclAccumDY += gdy;
  mclMutex.unlock();

  xInchesTracked += dXInches;

  lastXPos += rawDeltaDeg;
  dhAccumDegX = 0;  // X window consumed; start banking the heading swing afresh for the next step
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
  double sign = (wall <= 2) ? 1 : -1;

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
//target (e.g. turnPID.targetOffset) BEFORE wrapping — adding to an already-wrapped error
//would turn the long way around near the ±180 boundary.
double Odometry::angleToPoint(double targetX, double targetY, bool backward) {
  double targetH = atan2(targetX - x, targetY - y) * (180.0 / M_PI);
  if (backward) targetH += 180;
  return targetH;
}

double Odometry::wrappedPointHeadingError(double targetX, double targetY, bool backward) {
  return wrapAngle(angleToPoint(targetX, targetY, backward) - h);
}

double Odometry::positivePointHeadingError(double targetX, double targetY, bool backward) {
  double error = wrappedPointHeadingError(targetX, targetY, backward);
  if (error < 0) error += 360.0;
  return error;
}

double Odometry::negativePointHeadingError(double targetX, double targetY, bool backward) {
  double error = wrappedPointHeadingError(targetX, targetY, backward);
  if (error > 0) error -= 360.0;
  return error;
}

double Odometry::wrappedHeadingError(double targetH, bool backward) {
  if (backward) targetH += 180;
  return wrapAngle(targetH - h);
}

double Odometry::positiveHeadingError(double targetH, bool backward) {
  double error = wrappedHeadingError(targetH, backward);
  if (error < 0) error += 360.0;
  return error;
}

double Odometry::negativeHeadingError(double targetH, bool backward) {
  double error = wrappedHeadingError(targetH, backward);
  if (error > 0) error -= 360.0;
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
  if (backward) dist = -dist;
  return dist;
}

double Odometry::travelArcRadius() {
  return lastArcRadius;
}

//Signed forward displacement from a captured start point, projected onto the
//given heading axis (0 = north, clockwise-positive). The forward unit vector at
//heading h is (sin h, cos h), matching the position update in trackY().
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
    chassis.trackHeading();
    chassis.noEncodersUpdated = true;

    if (fabs(chassis.yRawDeltaDeg()) > chassis.yNoiseThreshold) {
      chassis.trackY(chassis.yRawDeltaDeg());
      chassis.noEncodersUpdated = false;
    }

    if (fabs(chassis.xRawDeltaDeg()) > chassis.xNoiseThreshold) {
      chassis.trackX(chassis.xRawDeltaDeg());
      chassis.noEncodersUpdated = false;
    }

    if (chassis.noEncodersUpdated) {
      delay(1);
    } else {
      chassis.applyPoseOffsets(chassis.xTrackerOffset, chassis.yTrackerOffset);
    }
  }
}