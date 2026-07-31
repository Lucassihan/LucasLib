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

  justResetPos = true;
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

  if (not hasSecondIMU or imu2Dead) {
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

  //Used for relative movements like drive()
  yInchesTracked += dYInches;

  double dhRad = TO_RAD(dhAccumDeg);
  lastArcRadius = (fabs(dhRad) < 1e-6) ? 1e9 : dYInches / dhRad;  // near-straight -> large sentinel
  dhAccumDeg = 0;

  lastYPos += rawDeltaDeg;
}

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

void Odometry::updateVelocity() {
  static uint64_t lastTime = vex::timer::systemHighResolution();

  uint64_t now = vex::timer::systemHighResolution();

  double timeSinceLastTracked = (now - lastTime) / 1000000.0;

  // field displacement
  double dx = x - lastVRefX;
  double dy = y - lastVRefY;

  // robot forward direction
  double headingRad = TO_RAD((h + lastVRefH) / 2.0);

  v = (dx * sin(headingRad) +
       dy * cos(headingRad)) / timeSinceLastTracked;

  ω = TO_RAD((h - lastVRefH) / timeSinceLastTracked);

  if (justResetPos) {
    v = 0;
    ω = 0;
    justResetPos = false;
  }

  lastVRefX = x;
  lastVRefY = y;
  lastVRefH = h;
  lastTime = now;
}

//readWall
void Odometry::readWall(int wall, int sensor, bool bypassSettleDelay) {
  double distanceReading = 0;
  DistanceSensorOffset mount;

  if (sensor == 1) {
    distanceReading = DistSensor1.objectDistance(inches);
    mount           = cfg.sensor1Offset;
  } else if (sensor == 2) {
    distanceReading = DistSensor2.objectDistance(inches);
    mount           = cfg.sensor2Offset;
  } else if (sensor == 3) {
    distanceReading = DistSensor3.objectDistance(inches);
    mount           = cfg.sensor3Offset;
  } else if (sensor == 4) {
    distanceReading = DistSensor4.objectDistance(inches);
    mount           = cfg.sensor4Offset;
  }

  // Rotate the sensor's 2D local mount into the field frame using the current heading
  // (0 = +Y, CW+): y (forward) runs along (sin h, cos h), x (right) along (cos h, -sin h).
  double hRad = TO_RAD(h);
  double mountFieldX = mount.y * sin(hRad) + mount.x * cos(hRad);
  double mountFieldY = mount.y * cos(hRad) - mount.x * sin(hRad);

  // walls 1 & 2 are positive edges (+48), walls 3 & 4 are negative edges (-48)
  double edgeSign = (wall <= 2) ? 1 : -1;

  // With the robot square to the wall, the reading is the perpendicular gap from the
  // sensor face to the wall, so the face sits at edgeSign*(48 - reading) on the wall's
  // axis. Subtracting the mount's component on that axis lands the tracking center.
  double faceAxis = edgeSign * (48.0 - distanceReading);

  debugger::readWallBefore();  // snapshot the pre-correction pose (no-op unless capturing)

  // walls 1 & 3 set X, walls 2 & 4 set Y
  //    2
  // 3     1
  //    4
  if (wall == 1 or wall == 3) {
    setPos(faceAxis - mountFieldX, y, h);
  } else if (wall == 2 or wall == 4) {
    setPos(x, faceAxis - mountFieldY, h);
  }

  debugger::readWall(wall, sensor);  // log the correction (pre snapshot -> corrected pose)
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

    if (fabs(chassis.yRawDeltaDeg()) > chassis.yNoiseThreshold) {
      chassis.trackY(chassis.yRawDeltaDeg());
      chassis.nEncodersUpdated += 1;
    }
    if (fabs(chassis.xRawDeltaDeg()) > chassis.xNoiseThreshold) {
      chassis.trackX(chassis.xRawDeltaDeg());
      chassis.nEncodersUpdated += 1;
    }

    if (chassis.nEncodersUpdated > 0) {
      chassis.applyPoseOffsets(chassis.xTrackerOffset, chassis.yTrackerOffset);
    }

    int needed = chassis.hasXTracker ? 2 : 1;
    if (chassis.nEncodersUpdated >= needed) {
      chassis.updateVelocity();
      chassis.nEncodersUpdated = 0;
    }

    delay(1);
  }
}