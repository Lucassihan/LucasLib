#pragma once
#include "vex.h"
#include "mcl/mclConfig.h"
#include <cmath>
#include <cstdlib>

// ============================================================================
// mcl.h -- Monte Carlo Localization for LucasLib.
//
// Modeled on 2654E "Echo": a cloud of (x, y) particles, heading taken from the
// IMU, weighted against the field walls by the distance sensors, then resampled.
// The filter is fed by the motion track() already computes, and its corrected
// (x, y) is blended back into `chassis`. Heading is never touched.
//
// Written to be as plain and readable as possible.
// ============================================================================

// ----------------------------- small helpers -------------------------------

// A random number between lo and hi (uniform). Cheap; good enough for noise.
inline double randomBetween(double lo, double hi) {
  double zeroToOne = double(rand()) / double(RAND_MAX);
  return lo + (hi - lo) * zeroToOne;
}

// Bell curve (normal shape): 1.0 at a perfect match, smaller as they differ.
inline double bellCurve(double expected, double measured, double spread) {
  double howFarOff = (expected - measured) / spread;
  return exp(-0.5 * howFarOff * howFarOff);
}

// Distance to the nearest perimeter wall from world point (sx, sy), looking along
// world heading phiRad. LucasLib convention: heading 0 = +Y, clockwise positive,
// so a ray at phi travels in direction (sin phi, cos phi).
inline double rayToWalls(double sx, double sy, double phiRad) {
  const double H = MCLConfig.FIELD_HALF;
  double dx = sin(phiRad);
  double dy = cos(phiRad);
  double best = MCLConfig.SENSOR_MAX_RANGE;

  if (dx > 1e-9) {
    double t = (H - sx) / dx;
    if (t > 0 && fabs(sy + t * dy) <= H) { best = fmin(best, t); }
  }
  if (dx < -1e-9) {
    double t = (-H - sx) / dx;
    if (t > 0 && fabs(sy + t * dy) <= H) { best = fmin(best, t); }
  }
  if (dy > 1e-9) {
    double t = (H - sy) / dy;
    if (t > 0 && fabs(sx + t * dx) <= H) { best = fmin(best, t); }
  }
  if (dy < -1e-9) {
    double t = (-H - sy) / dy;
    if (t > 0 && fabs(sx + t * dx) <= H) { best = fmin(best, t); }
  }
  return best;
}

// ------------------------------- a particle ---------------------------------

struct Particle {
  double x = 0;
  double y = 0;
};

// ---------------------------- the Sensor "plug" ------------------------------
// Each sensor answers: "how likely is my reading if the robot were at (px, py)?"
// It either returns a weight, or ABSTAINS (no useful reading) and sits out the vote.

// A sensor's vote. We use this tiny struct instead of std::optional because the
// VEX toolchain's libstdc++ (GCC 4.9) predates <optional>.
struct SensorVote {
  bool   valid;    // false = abstain (no usable reading this frame) -> skip this sensor
  double weight;   // the likelihood, used only when valid
};

class Sensor {
public:
  virtual SensorVote p(double px, double py, double headingRad) = 0;
  virtual void update() = 0;   // read the hardware ONCE per frame
  virtual ~Sensor() {}
};

// ------------------------- distance sensor -> walls --------------------------

class MCLDistanceSensor : public Sensor {
public:
  MCLDistanceSensor() {}

  // mountForward  = inches ahead of tracking center (back-facing sensor: negative)
  // mountSideways = inches to the right of center   (left sensor: negative)
  // mountAngleRad = facing vs robot front, in RADIANS (converted once at setup)
  // scaleConstant = per-sensor fix (~1.00-1.03), sensors read a few % high
  void configure(vex::distance* device, double mountForward, double mountSideways,
                 double mountAngleRad, double scaleConstant) {
    this->device        = device;
    this->mountForward  = mountForward;
    this->mountSideways = mountSideways;
    this->mountAngleRad = mountAngleRad;
    this->scaleConstant = scaleConstant;
  }

  // Read the hardware once; reused for every particle this frame.
  void update() override {
    hasValidReading = device->isObjectDetected();

    double rawMillimeters = device->objectDistance(vex::distanceUnits::mm);
    measuredInches = scaleConstant * rawMillimeters / 25.4;

    if (measuredInches > MCLConfig.SENSOR_MAX_RANGE) {
      hasValidReading = false;            // too far away to trust
    }

    standardDeviation = MCLConfig.SENSOR_STD_FRAC * measuredInches;
    if (standardDeviation < MCLConfig.SENSOR_STD_FLOOR) {
      standardDeviation = MCLConfig.SENSOR_STD_FLOOR;
    }
  }

  // How well does this particle explain the reading we just took?
  SensorVote p(double px, double py, double headingRad) override {
    if (!hasValidReading) {
      return { false, 0.0 };              // abstain this frame
    }

    // 1. where is the sensor in the field? (rotate its robot-frame mount by heading)
    double sensorX = px + mountForward * sin(headingRad) + mountSideways * cos(headingRad);
    double sensorY = py + mountForward * cos(headingRad) - mountSideways * sin(headingRad);

    // 2. which way is the sensor pointing in the field? (mount angle is already radians)
    double sensorHeadingRad = headingRad + mountAngleRad;

    // 3. how far SHOULD it read to the wall from here?
    double expectedInches = rayToWalls(sensorX, sensorY, sensorHeadingRad);

    // 4. score the match. Floor it (heavy tail) so a single wildly-wrong reading
    //    -- e.g. an opponent in the beam, or a wall just out of range -- can't drive
    //    a good particle's whole weight to zero.
    double score = bellCurve(expectedInches, measuredInches, standardDeviation);
    if (score < MCLConfig.HEAVY_TAIL_FLOOR) {
      score = MCLConfig.HEAVY_TAIL_FLOOR;
    }
    return { true, score * MCLConfig.DISTANCE_WEIGHT };
  }

private:
  vex::distance* device = nullptr;
  double mountForward = 0, mountSideways = 0, mountAngleRad = 0, scaleConstant = 1.0;
  double measuredInches = 0;
  double standardDeviation = 1.0;
  bool   hasValidReading = false;
};

// ------------------------------ the filter -----------------------------------

class ParticleFilter {
public:
  Particle estimate;            // average particle -> blended into chassis

  void seedAround(double centerX, double centerY) {
    for (int i = 0; i < N; i++) {
      particles[i].x = centerX + randomBetween(-MCLConfig.SEED_STD, MCLConfig.SEED_STD);
      particles[i].y = centerY + randomBetween(-MCLConfig.SEED_STD, MCLConfig.SEED_STD);
    }
    estimate.x = centerX;
    estimate.y = centerY;
  }

  void seedUniform() {          // "totally lost" recovery
    for (int i = 0; i < N; i++) {
      particles[i].x = randomBetween(-MCLConfig.FIELD_HALF, MCLConfig.FIELD_HALF);
      particles[i].y = randomBetween(-MCLConfig.FIELD_HALF, MCLConfig.FIELD_HALF);
    }
  }

  void addSensor(Sensor* sensor) {
    if (sensorCount < MAX_SENSORS) {
      sensors[sensorCount] = sensor;
      sensorCount++;
    }
  }

  // One MCL cycle.
  //   globalDX, globalDY = how far the robot moved (field frame) since last call
  //   headingRad         = trusted IMU heading right now
  //   slipAccel          = centripetal accel v^2/r right now (sideways slip risk)
  //   nowMs              = current time in milliseconds (for the update gate)
  void step(double globalDX, double globalDY, double headingRad,
            double slipAccel, uint32_t nowMs) {
    injectRandomParticles();    // sprinkle in a few fresh guesses so we can recover if lost
    moveEveryParticle(globalDX, globalDY, headingRad, slipAccel);
    distanceSinceUpdate += sqrt(globalDX * globalDX + globalDY * globalDY);

    // Don't do the expensive sensor weighting until we've actually moved enough
    // (or waited long enough). Resampling while sitting still collapses the cloud.
    double secondsSinceUpdate = double(nowMs - lastUpdateMs) / 1000.0;
    bool movedEnough  = distanceSinceUpdate >= MCLConfig.MIN_UPDATE_DIST;
    bool waitedEnough = secondsSinceUpdate >= MCLConfig.MAX_NO_UPDATE_TIME;
    if (!movedEnough && !waitedEnough) {
      updateEstimate();
      return;
    }
    if (sensorCount == 0) {
      updateEstimate();
      return;
    }

    readEverySensorOnce();
    double totalWeight = weightEveryParticle(headingRad);
    if (totalWeight <= 0.0) {
      updateEstimate();           // everyone disagreed; don't resample on garbage
      return;
    }

    resample();
    updateEstimate();
    distanceSinceUpdate = 0.0;
    lastUpdateMs = nowMs;
  }

private:
  static constexpr int N = MclConfig::NUM_PARTICLES;
  static constexpr int MAX_SENSORS = 8;

  Particle particles[N];
  Particle oldParticles[N];
  double   weights[N];
  Sensor*  sensors[MAX_SENSORS];
  int      sensorCount = 0;
  double   distanceSinceUpdate = 0.0;
  uint32_t lastUpdateMs = 0;

  // Augmented MCL: replace a few particles with brand-new random guesses each step.
  // If the cloud has drifted off the truth (a low-observability moment, or a kidnap),
  // these let it rediscover where it really is. They get ~0 weight when we're tracking
  // fine, so they cost almost nothing in the normal case.
  void injectRandomParticles() {
    int count = int(MCLConfig.INJECT_FRAC * double(N));
    for (int j = 0; j < count; j++) {
      int i = rand() % N;
      particles[i].x = randomBetween(-MCLConfig.FIELD_HALF, MCLConfig.FIELD_HALF);
      particles[i].y = randomBetween(-MCLConfig.FIELD_HALF, MCLConfig.FIELD_HALF);
    }
  }

  // PREDICT: move every particle the way the robot moved, plus per-particle noise.
  // More noise sideways than forward (no lateral tracker), and EXTRA sideways noise
  // once we exceed the measured slip limit (the robot is actually sliding).
  void moveEveryParticle(double globalDX, double globalDY, double headingRad, double slipAccel) {
    double distanceMoved = sqrt(globalDX * globalDX + globalDY * globalDY);

    double excessAccel = slipAccel - MCLConfig.maxLateralAccel();
    if (excessAccel < 0.0) {
      excessAccel = 0.0;                  // friction holds; no extra noise
    }
    double sidewaysFraction = MCLConfig.SIDEWAYS_NOISE + MCLConfig.SLIP_GAIN * excessAccel;

    for (int i = 0; i < N; i++) {
      // noise in the robot's own frame
      double forwardNoise  = randomBetween(-1.0, 1.0) * MCLConfig.FORWARD_NOISE * distanceMoved;
      double sidewaysNoise = randomBetween(-1.0, 1.0) * sidewaysFraction        * distanceMoved;

      // rotate that into field X/Y (forward points along heading; sideways is 90 deg right)
      double noiseX = forwardNoise * sin(headingRad) + sidewaysNoise * cos(headingRad);
      double noiseY = forwardNoise * cos(headingRad) - sidewaysNoise * sin(headingRad);

      particles[i].x += globalDX + noiseX;
      particles[i].y += globalDY + noiseY;

      clampToField(particles[i]);   // a robot can't be outside the walls
    }
  }

  // keep a particle inside the field (the robot physically can't leave it)
  void clampToField(Particle& p) {
    double H = MCLConfig.FIELD_HALF;
    if (p.x >  H) { p.x =  H; }
    if (p.x < -H) { p.x = -H; }
    if (p.y >  H) { p.y =  H; }
    if (p.y < -H) { p.y = -H; }
  }

  void readEverySensorOnce() {
    for (int s = 0; s < sensorCount; s++) {
      sensors[s]->update();
    }
  }

  // weight = how well each particle agrees with ALL the sensors (product of votes)
  double weightEveryParticle(double headingRad) {
    double totalWeight = 0.0;
    for (int i = 0; i < N; i++) {
      double weight = 1.0;
      for (int s = 0; s < sensorCount; s++) {
        SensorVote vote = sensors[s]->p(particles[i].x, particles[i].y, headingRad);
        if (vote.valid) {
          weight = weight * vote.weight;
        }
        // abstained sensors are simply skipped
      }
      weights[i] = weight;
      totalWeight += weight;
    }
    return totalWeight;
  }

  // RESAMPLE (systematic): lay weights end-to-end, drop N evenly-spaced points,
  // keep whichever particle each point lands on. Clones the heavy, drops the light.
  void resample() {
    double totalWeight = 0.0;
    for (int i = 0; i < N; i++) {
      totalWeight += weights[i];
    }
    double spacing = totalWeight / double(N);
    double firstPoint = randomBetween(0.0, spacing);

    for (int i = 0; i < N; i++) {
      oldParticles[i] = particles[i];
    }

    int sourceIndex = 0;
    double weightSoFar = weights[0];
    for (int newIndex = 0; newIndex < N; newIndex++) {
      double thisPoint = firstPoint + double(newIndex) * spacing;
      while (thisPoint > weightSoFar && sourceIndex < N - 1) {
        sourceIndex++;
        weightSoFar += weights[sourceIndex];
      }
      // copy the chosen particle, then ROUGHEN it (a little jitter) so the cloud keeps
      // its spread instead of collapsing onto a few duplicated points.
      particles[newIndex].x = oldParticles[sourceIndex].x + randomBetween(-1.0, 1.0) * MCLConfig.ROUGHEN_STD;
      particles[newIndex].y = oldParticles[sourceIndex].y + randomBetween(-1.0, 1.0) * MCLConfig.ROUGHEN_STD;
      clampToField(particles[newIndex]);
    }
  }

  void updateEstimate() {
    double sumX = 0.0;
    double sumY = 0.0;
    for (int i = 0; i < N; i++) {
      sumX += particles[i].x;
      sumY += particles[i].y;
    }
    estimate.x = sumX / double(N);
    estimate.y = sumY / double(N);
  }

  bool isOutsideField(const Particle& p) {
    double H = MCLConfig.FIELD_HALF;
    return p.x > H || p.x < -H || p.y > H || p.y < -H;
  }
};

// ------------------------------- public API ----------------------------------
// Call StartMCL(x, y) at the start of autonomous to seed the cloud at a known pose
// (call it again after any chassis.setPos). Launch mclTask() as a vex::task once.
void StartMCL(double startX, double startY);   // seed + turn MCL ON
void StopMCL();                                 // turn MCL OFF -> pure odometry
int  mclTask();
