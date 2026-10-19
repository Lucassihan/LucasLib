#include "movement/odometry.h"
#include "ramsete/util/ramseteController.h"
#include "coreConfigs.h"
#include "util.h"
#include "misc/sylibUtilities.h"
#include "debugger/debugger.h"

#include <cmath>

static motor* pickMotor(DriveEncoderPosition pos, motor& f, motor& m, motor& b) {
  if (pos == middle) return &m;
  if (pos == back)   return &b;
  return &f;
}

//constructor
Odometry::Odometry(OdometryConfig config) : odomConfigs(config) {
  hasYTracker  = (odomConfigs.yTracker  != nullptr);
  hasXTracker  = (odomConfigs.xTracker  != nullptr);
  hasSecondIMU = (odomConfigs.imu2      != nullptr);

  leftEncoder  = pickMotor(odomConfigs.driveEncoder, fl, ml, bl);
  rightEncoder = pickMotor(odomConfigs.driveEncoder, fr, mr, br);

  lastIMU1     = odomConfigs.imu1->rotation(rotationUnits::deg);
  lastLeftPos  = leftEncoder->position(rotationUnits::deg);
  lastRightPos = rightEncoder->position(rotationUnits::deg);

  if (hasSecondIMU) {
    lastIMU2 = odomConfigs.imu2->rotation(rotationUnits::deg);
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
  xTrackerOffset = odomConfigs.xTrackerOffset;
  yTrackerOffset = odomConfigs.yTrackerOffset;

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

  // The heading reference jumps with the pose. justResetPos alone isn't enough: it is only
  // consumed inside updateVelocity(), which is gated on both trackers having moved, so a robot
  // standing still after a setPos can hold a stale reference indefinitely, and the teleport then
  // reads as real rotation. (v is immune — it comes off the encoder, which a setPos never touches.)
  lastVRefH = h;

  justResetPos = true;
}

//conversion helpers
double Odometry::motorDegsToInches(double deg) {
  return deg * (odomConfigs.driveWheelDiameter * M_PI * odomConfigs.driveGearRatio) / 360.0;
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

//Degrees per second the vertical encoder reads at the drive's free speed. Only the sylib gain
//schedule uses it, to know what counts as a big acceleration on this device.
double Odometry::yEncoderFullScale() {
  double cartridgeRPM = 200;
  gearSetting cartridge = leftEncoder->getMotorCartridge();
  if (cartridge == ratio36_1) cartridgeRPM = 100;
  else if (cartridge == ratio6_1) cartridgeRPM = 600;

  //Drive encoders read the motor's own output shaft, so the external gear ratio isn't in it yet.
  if (not hasYTracker or odomConfigs.yTrackerDiameter <= 0) return cartridgeRPM * 6;

  double freeSpeed = cartridgeRPM * odomConfigs.driveGearRatio / 60.0
                     * M_PI * odomConfigs.driveWheelDiameter;   // in/s

  return freeSpeed / (M_PI * odomConfigs.yTrackerDiameter) * 360.0;
}

//Fills v and vRaw: forward speed of the tracking center, measured straight off the vertical
//encoder, with the sylib estimator supplying the degrees-per-second and the config geometry
//turning those into inches. Nothing from the fused pose is in the path, so a setPos or a wall
//reset cannot show up here as motion, and a heading error cannot bleed into the speed.
//No smoothing switch on the way out either: the EMA inside the estimator schedules its own
//window from measured acceleration, so there is no fixed sample count left for anyone to pick.
//Runs every tick — the estimator gates itself on the device's clock, and it wants a call per
//sample, not a call per accumulation.
void Odometry::updateEncoderVelocity() {
  double   position  = 0;
  uint32_t timestamp = 0;

  if (hasYTracker) {
    position  = odomConfigs.yTracker->position(rotationUnits::deg);
    timestamp = odomConfigs.yTracker->timestamp();
  } else {
    //The positions come through the motor objects, not vexMotorPositionRawGet, because the raw
    //call ignores the reversed flag the left motors are built with — averaging raw counts would
    //cancel a straight drive to zero. The raw call is still how the motor's own update clock is
    //read, so it is made for the timestamp and the position it returns is thrown away.
    uint32_t motorClock = 0;
    vexMotorPositionRawGet(leftEncoder->index(), &motorClock);

    //Common mode is forward travel; a turn cancels. One clock covers the pair, the two motors
    //being at most a frame apart.
    position  = (leftEncoder->position(rotationUnits::deg)
                 + rightEncoder->position(rotationUnits::deg)) / 2.0;
    timestamp = motorClock;
  }

  double filteredDegPerSec = encoderVelocity(position, timestamp, yEncoderFullScale());
  double rawDegPerSec      = rawEncoderVelocity();

  if (hasYTracker) {
    v    = trackerDegsToInches(filteredDegPerSec, odomConfigs.yTrackerDiameter);
    vRaw = trackerDegsToInches(rawDegPerSec,      odomConfigs.yTrackerDiameter);

    //One tracker reads the speed of wherever it sits, and the tracking center sits ω times the
    //lateral offset away — the same yTrackerOffset applyPoseOffsets uses on the position, so the
    //two agree on where they are measuring.
    v    += ω * yTrackerOffset;
    vRaw += ω * yTrackerOffset;
  } else {
    //The drive pair straddles the center: the left wheel runs v + ω·trackwidth/2 and the right
    //runs v - ω·trackwidth/2, so their average is the center's speed with the turn already
    //cancelled. No offset correction to make.
    v    = motorDegsToInches(filteredDegPerSec);
    vRaw = motorDegsToInches(rawDegPerSec);
  }
}

//Raw Y reading (degrees), BEFORE scaling: the Y tracker if present, otherwise the average
//of the two drive encoders (their common-mode motion is forward travel).
double Odometry::yRawPosition() {
  if (hasYTracker) return odomConfigs.yTracker->position(rotationUnits::deg);
  return (leftEncoder->position(rotationUnits::deg) + rightEncoder->position(rotationUnits::deg)) / 2.0;
}

//Raw X reading (degrees), BEFORE scaling: the X tracker if present, otherwise 0 (no lateral tracking).
double Odometry::xRawPosition() {
  return hasXTracker ? odomConfigs.xTracker->position(rotationUnits::deg) : 0;
}

//heading resolution
double Odometry::resolveHeading(double dIMU1, double dIMU2, double dLDeg, double dRDeg) {
  double scaled1 = dIMU1 * odomConfigs.imu1_multiplier;
  double scaled2 = dIMU2 * odomConfigs.imu2_multiplier;

  if (not hasSecondIMU or imu2Dead) {
    return scaled1;
  }

  if (imu1Dead) {
    return scaled2;
  }

  if (fabs(scaled1 - scaled2) >= 5.0) {
    double dhWheelsDeg = (motorDegsToInches(dRDeg) - motorDegsToInches(dLDeg))
                         / odomConfigs.trackwidth * (180.0 / M_PI);

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
  double curIMU1 = odomConfigs.imu1->rotation(rotationUnits::deg);
  double curIMU2 = hasSecondIMU ? odomConfigs.imu2->rotation(rotationUnits::deg) : 0;
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

  x = trueX - gx;
  y = trueY - gy;
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
  double dYInches = hasYTracker ? trackerDegsToInches(rawDeltaDeg, odomConfigs.yTrackerDiameter)
                                : motorDegsToInches(rawDeltaDeg);

  double headingRad = TO_RAD(h - dhAccumDeg / 2.0);  // window midpoint, not end
  double gdx = dYInches * sin(headingRad);
  double gdy = dYInches * cos(headingRad);

  trueX += gdx;
  trueY += gdy;

  //Used for relative movements like driveForward()/driveBackward()
  yInchesTracked += dYInches;

  double dhRad = TO_RAD(dhAccumDeg);
  lastArcRadius = (fabs(dhRad) < 1e-6) ? 1e9 : dYInches / dhRad;  // near-straight -> large sentinel
  dhAccumDeg = 0;

  lastYPos += rawDeltaDeg;
}

void Odometry::trackX(double rawDeltaDeg) {
  double dXInches = trackerDegsToInches(rawDeltaDeg, odomConfigs.xTrackerDiameter);

  double headingRad = TO_RAD(h - dhAccumDegX / 2.0);  // window midpoint, not end
  double gdx =  dXInches * cos(headingRad);
  double gdy = -dXInches * sin(headingRad);

  trueX += gdx;
  trueY += gdy;

  xInchesTracked += dXInches;

  lastXPos += rawDeltaDeg;
  dhAccumDegX = 0;  // X window consumed; start banking the heading swing afresh for the next step
}

void Odometry::updateVelocity() {
  static uint64_t lastTime = vex::timer::systemHighResolution();

  uint64_t now = vex::timer::systemHighResolution();

  double timeSinceLastTracked = (now - lastTime) / 1000000.0;

  // Two samples inside the same scheduler tick divide a real rotation by almost nothing, and the
  // result is not a fast robot — it's thousands of deg/s. That lands in the turn channel's error
  // term as kP·(target − ω) and pins the drive at 12 V. Hold the previous ω and let the rotation
  // accumulate until a real interval has passed; returning early deliberately leaves lastVRefH
  // alone so nothing is lost.
  if (timeSinceLastTracked < 0.0005) {
    return;
  }

  ωRaw = TO_RAD((h - lastVRefH) / timeSinceLastTracked);

  // Before the history sees it: a teleport's reading is not a measurement, and averaging it in
  // would spread one bad sample across the next five.
  if (justResetPos) {
    ωRaw = 0;
    justResetPos = false;
  }

  // Pushed either way, so switching smoothing on mid-run reads a warm history instead of ramping
  // up from zero.
  double ωSmoothed = ωHistory.push(ωRaw, ramsete.smoothedωSamples);

  ω = ramsete.smoothω ? ωSmoothed : ωRaw;

  lastVRefH = h;
  lastTime = now;
}

//readWall
void Odometry::readWall(int wall, int sensor, bool bypassSettleDelay) {
  double distanceReading = 0;
  DistanceSensorOffset mount;

  if (sensor == 1) {
    distanceReading = DistSensor1.objectDistance(inches);
    mount           = odomConfigs.sensor1Offset;
  } else if (sensor == 2) {
    distanceReading = DistSensor2.objectDistance(inches);
    mount           = odomConfigs.sensor2Offset;
  } else if (sensor == 3) {
    distanceReading = DistSensor3.objectDistance(inches);
    mount           = odomConfigs.sensor3Offset;
  } else if (sensor == 4) {
    distanceReading = DistSensor4.objectDistance(inches);
    mount           = odomConfigs.sensor4Offset;
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

double Odometry::wrappedHeadingError(double targetH) {
  return wrapAngle(targetH - h);
}

double Odometry::positiveHeadingError(double targetH) {
  double error = wrappedHeadingError(targetH);
  if (error < 0) error += 360.0;
  return error;
}

double Odometry::negativeHeadingError(double targetH) {
  double error = wrappedHeadingError(targetH);
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
//odometer: it reads straight off (x, y), so movement error follows the pose
//rather than raw encoders, and a pose correction just changes the current
//reading instead of being permanently accumulated.
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

    //Last, so the tracker's off-center correction uses this tick's ω rather than the previous one.
    chassis.updateEncoderVelocity();

    delay(1);
  }
}