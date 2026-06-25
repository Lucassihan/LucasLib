# Proposal: Monte Carlo Localization for LucasLib

> **BUILT & VALIDATED (off-robot).** The module exists: `include/mcl/{mcl.h, mclConfig.h}`,
> `src/mcl/{mcl.cpp, mclConfig.cpp}`, plus the `track()` accumulator (`odometry.h/.cpp`),
> the task launch (`tasks.cpp`), and the makefile include path. Compiled with `clang++`
> against stub VEX headers and driven through a simulated robot:
> - **Tracking:** blended pose **0.33 in** avg vs pure-odometry 0.66 in — **MCL beats odometry 2×**, error bounded.
> - **Shove recovery:** an 8 in push that odometry misses is corrected in **~2 s**.
> - `mcl.cpp` task glue and the filter both compile clean.
>
> **Three robustness features became CORE (the test proved they're required for a wall-only
> filter), not the "optional" toggles the early draft implied:** ① clamp particles to the field,
> ② heavy-tail likelihood floor (one bad/saturated sensor can't zero a particle), ③ roughening
> after resample (prevents cloud collapse). **Random injection is OFF by default** — it biases
> *unobserved* axes toward field-center and made shove-recovery *worse* in testing; turn it on
> (`INJECT_FRAC ≈ 0.02`) only if you specifically need kidnap recovery.
>
> **Observability caveat:** with 4 sensors at 90° spacing, near a wall the in-range sensors can
> all see the *same* wall, leaving one axis momentarily unobserved. Odometry holds that axis (the
> blend only nudges), and roughening lets MCL re-acquire it — but place sensors at varied angles
> and prefer paths that aren't wall-hugging. **Still must be tuned on the real robot** (sensor
> mounts, `SIDE_PUSH_FORCE`/`ROBOT_WEIGHT`, noise) before trusting it.



**Modeled heavily on 2654E "Echo" (alexDickhans)** — the only tried-and-true VEX MCL — translated to VEXcode Pro V5 C++ and fitted onto LucasLib's existing `Odometry`/`chassis`. Every deviation from Echo is justified by a decision recorded in `mcl-vex-site/FEATURES_AND_DECISIONS.md`.

> Style: VEXcode (`vex::`), inches, heading in **degrees, VEX convention** (0° = +Y, clockwise-positive), braces always, **no ternaries** (per CLAUDE.md). Code below is a blueprint, not final.

> **Readability mandate (team):** write everything as STUPID and READABLE as possible — plain named variables, explicit loops, obvious helper functions, comments that say *why*. Sacrificing a little compute for clarity is fine. Only keep something terse if making it readable would cost *significant* compute (the per-particle weight loop and the resample comb are the only such spots, and they're heavily commented).

## Given constraints (from the team)

1. **Guaranteed exactly 4 distance sensors**, each with a **2-axis mount offset** (forward *and* sideways) from the tracking center. → no "fewer than 4" handling; each sensor stores `mountForward` **and** `mountSideways`.
2. **(Possibly dual) IMU heading already lives in the odometry class** (`resolveHeading` → `chassis.h`). MCL just reads it; it never estimates or corrects heading.
3. **No sideways (horizontal) tracker wheel.** → predict adds **more noise sideways than forward** — a constant *base* amount, since sideways is the unmeasured axis — and then **EXTRA** sideways noise grows *on top of that base* once centripetal accel `v²/r` exceeds a *measured* slip limit. That limit is set by a **force** knob you measure (`SIDE_PUSH_FORCE` = the sideways push it takes to make the wheels slide), shared with boomerang (which uses it to cap speed); see §4b. Reuses `chassis.velocity` and `chassis.lastArcRadius` (both already public).

---

## 0. Why LucasLib is already most of the way there

| Echo needs… | LucasLib already has… |
|---|---|
| Per-cycle local displacement (arc-chord, IMU Δθ) | `track()` lines 120–155: `chordFactor = 2·sin(dhRad/2)`, Δθ→0 guard, average-heading rotation — **identical to our decided predict increment** |
| A trusted absolute heading | dual-IMU `resolveHeading()` → `chassis.h` |
| Distance-sensor mounting offsets | `OdometryConfig::sensor1Offset … sensor4Offset` |
| A wall model | `readWall()` already reasons about walls + offsets |
| A shared pose movement reads | `extern Odometry chassis;` (`chassis.x/y/h`) consumed by `movement.cpp`, `auton.cpp`, etc. |

**So the work is: add a particle filter that consumes the increment `track()` already produces, weights particles against the walls with the distance sensors, and blends its corrected (x, y) back into `chassis`.** Heading stays entirely on `resolveHeading` — MCL never touches `chassis.h`.

---

## 1. Fidelity to 2654E (what we keep verbatim)

- **2-D particles `(x, y)`; heading shared from the IMU.** Solve only position. *(Echo: particles are `[x,y]`, heading from `angleFunction()`.)*
- **Pluggable `Sensor` interface** returning `std::optional<double>` (abstain = skip). Weight = product across sensors.
- **Distance-gated updates**: run weight+resample only after ≥ `MIN_UPDATE_DIST` (2 in) **or** ≥ `MAX_NO_UPDATE_TIME` (2 s). Prevents stationary collapse and limits single-bad-reading influence.
- **Read each sensor once per frame**, reuse across all particles.
- **Uniform noise** (not Gaussian) for ~6× speed.
- **Systematic / stratified resampling** (the low-variance comb).
- **Out-of-field particles → uniform random reinit** for variety/recovery.
- **Spec-derived distance σ** (≈2.5 % of reading) + **per-sensor tuning constant** (sensors read 1–3 % high).
- **Estimate = mean of particles.**
- **Sensor suite philosophy**: redundancy first — many heterogeneous sensors so one failure doesn't sink localization.

## 2. Deviations from Echo (each justified)

| Change | Why |
|---|---|
| **Predict reuses `track()`'s arc-chord `localDX/localDY`** instead of Echo's straight-line forward step | LucasLib already computes the better increment for free; arc-chord with IMU Δθ is ~O(1)/cycle and strictly ≥ accuracy (register §2). Echo only used straight-line for simplicity. |
| **Blend the correction into `chassis` (`α≈0.35`)** instead of using the PF mean as the pose directly | Avoids jitter/teleport into PID movement (7700R). Increment-based blend, not averaging a free-running pose, so drift can't leak (register §6). `α = 1.0` recovers Echo's direct-replacement. |
| **VEXcode distance API**: gate on `isObjectDetected()` + range cap; σ from datasheet only | VEXcode `vex::distance` has **no confidence accessor** — only `objectDistance()`, `objectSize()` (an *enum*: none/small/medium/large), `isObjectDetected()`, `objectVelocity()`, `installed()`. (PROS-only: `get_confidence()` 0–63.) `objectSize` also varies foam-vs-metal field, so we don't gate on it either. → σ = 2.5 %·reading alone. |
| **Do NOT gate on `objectSize`** | It varies foam-vs-metal field (Dickhans 19:13). |
| **Anisotropic predict noise** (more sideways than forward) + **friction-threshold slip noise** (a constant base, plus **EXTRA** that turns on once centripetal accel `v²/r` exceeds the *measured* slip limit) | Sideways is the unmeasured axis; past the measured static-friction limit the robot truly slides, so spread should grow there. The limit is one **shared, force-based** knob (`SIDE_PUSH_FORCE` → `MAX_LATERAL_ACCEL`) — boomerang caps speed with it, MCL models slip. Reuses existing `velocity` + `lastArcRadius`. |
| Optional **heavy-tail likelihood clamp** + **~2 % random injection**, *off by default* | Our robustness additions; kept optional so the core stays exactly Echo's proven design. |

---

## 3. File layout (an `mcl/` module — two headers, two sources)

```
mcl/
├── mclConfig.h     all the tunable knobs (declared) + the shared RobotPhysical
│                   slip constants that boomerang also reads
├── mclConfig.cpp   the actual numbers you tune — one place to edit values
├── mcl.h           Particle · Sensor · MCLDistanceSensor · ParticleFilter
│                   + the facade: StartMCL(startX, startY) and mclTask()
└── mcl.cpp         all the implementation + the 4 sensor objects + the task loop
```

Everything MCL lives in this one folder. **`mclConfig.*`** = the knobs (and the shared
slip-force constants); **`mcl.*`** = all the logic. The class/function sketches in the
sections below all map into these four files (noted per section).

> VEXcode build note: put `mcl/` where your build compiles sources with headers on the
> include path. If you keep LucasLib's include/src split, it's
> `include/mcl/{mcl.h, mclConfig.h}` + `src/mcl/{mcl.cpp, mclConfig.cpp}` — same four files.

---

## 4. Config (`mclConfig.h` + `mclConfig.cpp`)

```cpp
#pragma once

namespace MCLConfig {
  constexpr int    NUM_PARTICLES      = 400;     // <=500 (Echo's frame-time ceiling)
  constexpr double FIELD_HALF         = 70.4375; // in, wall coordinate

  // predict noise (uniform half-widths, as a fraction of how far we moved this step).
  // SIDEWAYS is bigger than FORWARD: this robot has a forward tracker wheel but NO
  // sideways tracker wheel, so we trust sideways motion much less. Tune on the robot.
  constexpr double FORWARD_NOISE      = 0.08;    // along the heading (we measure this well)
  constexpr double SIDEWAYS_NOISE     = 0.20;    // base sideways noise (no tracker here)

  // Extra sideways noise kicks in only when centripetal accel (v^2/r) EXCEEDS the
  // measured static-friction limit RobotPhysical::MAX_LATERAL_ACCEL -- i.e. when the
  // robot is actually sliding. Noise grows with that excess. (gain units: s^2/in)
  constexpr double SLIP_GAIN          = 0.003;   // tune on the robot

  // update gating
  constexpr double MIN_UPDATE_DIST    = 2.0;     // in
  constexpr double MAX_NO_UPDATE_TIME= 2.0;     // s

  // distance sensor model
  constexpr double SENSOR_MAX_RANGE   = 78.0;    // in (~2 m practical cap)
  constexpr double SENSOR_STD_FRAC    = 0.025;   // 2.5% of reading (datasheet-derived)
  constexpr double SENSOR_STD_FLOOR   = 0.4;     // in, minimum std
  constexpr double DISTANCE_WEIGHT    = 1.0;

  // integration
  constexpr double MCL_WEIGHT         = 0.35;    // 7700R; 1.0 == Echo direct-replacement
  constexpr double SEED_STD           = 1.5;     // in, initial scatter
  constexpr double INJECT_FRAC        = 0.0;     // optional augmented-MCL (0 = off)

  constexpr int    TASK_DELAY_MS      = 20;      // ~50 Hz mcl task
}
```

---

## 4b. Shared physics: the lateral-slip limit (boomerang AND MCL use it)

Lives in **`mcl/mclConfig.h`** so there's one source of truth; boomerang `#include`s it too.
The knob YOU set is a **force** — the sideways push it takes to make the wheels slide.

```cpp
namespace RobotPhysical {
  // ===== the two numbers you MEASURE with a scale (both in kg) =====
  // SIDE_PUSH_FORCE is THE sideslip knob: the sideways pull needed to make the wheels
  // start sliding. Bigger number = more grip = less slip noise (and a higher speed cap).
  constexpr double SIDE_PUSH_FORCE = 3.5;   // kg  (lateral pull to break the wheels loose)
  constexpr double ROBOT_WEIGHT    = 5.0;   // kg  (the robot weighed on the scale)

  // ===== derived -- do not edit =====
  // The wheels slide when the sideways force (mass * v^2/r) beats the push force.
  // Dividing by mass turns that into an acceleration limit; the mass cancels into
  // the push/weight ratio (which is just the coefficient of friction).
  constexpr double GRAVITY_IN_S2     = 386.09;  // g, in inches/second^2
  constexpr double MAX_LATERAL_ACCEL = (SIDE_PUSH_FORCE / ROBOT_WEIGHT) * GRAVITY_IN_S2; // in/s^2
}

// Sideways pull on the robot right now: centripetal acceleration a = v^2 / r.
// Straight driving -> arcRadius huge -> a ~ 0.  Tight fast turn -> small radius -> big a.
inline double centripetalAcceleration(double velocity, double arcRadius) {
  double radius = fabs(arcRadius);
  if (radius < 1.0) {
    radius = 1.0;                 // guard a near-zero radius from blowing up the division
  }
  return (velocity * velocity) / radius;
}
```

**Measuring the two numbers (a few minutes, one scale):**
1. Weigh the robot on the field tiles → `ROBOT_WEIGHT` (kg).
2. Hook the scale low on the chassis, pull SIDEWAYS slowly until the wheels just break loose
   and slide; record the peak reading right before it slides → `SIDE_PUSH_FORCE` (kg).
   Pull low so it slides, not tips. Do it on the foam tiles; average a few pulls.

**Two complementary uses of the same limit:**
- **Boomerang LIMITS speed** to stay under it:
  `vMax = sqrt(SAFETY * RobotPhysical::MAX_LATERAL_ACCEL * fabs(chassis.lastArcRadius))`, `SAFETY ≈ 0.6–0.8`. Don't ask for more sideways grip than you have.
- **MCL MODELS the slip** that still happens (defense, a push, driver, overshoot): **EXTRA** sideways noise — on top of the base `SIDEWAYS_NOISE` — turns on only once `centripetalAcceleration(...) > MAX_LATERAL_ACCEL`.
- **Nice consequence:** while boomerang respects the limit, MCL adds ~zero extra slip noise; the extra fires only when the robot is genuinely sliding.

---

## 5. Particle, Field, ray-cast (in `mcl.h`)

Heading is shared, so a particle is just position. Ray-cast uses LucasLib's convention: a world direction at VEX heading `φ` has unit vector **(sin φ, cos φ)** — matching `track()`'s `x += localDY·sin + localDX·cos`.

```cpp
#pragma once
#include "vex.h"
#include "mcl/mclConfig.h"
#include <cmath>
#include <cstdlib>

struct Particle {
  double x = 0;
  double y = 0;
};

// A random number between lo and hi (uniform). Cheap; good enough for filter noise.
inline double randomBetween(double lo, double hi) {
  double zeroToOne = double(rand()) / double(RAND_MAX);
  return lo + (hi - lo) * zeroToOne;
}

// Bell curve (normal shape): 1.0 at a perfect match, smaller as expected and
// measured drift apart. `spread` is the standard deviation. Used to score sensors.
inline double bellCurve(double expected, double measured, double spread) {
  double howFarOff = (expected - measured) / spread;
  return exp(-0.5 * howFarOff * howFarOff);
}

// NOTE: centripetalAcceleration() and MAX_LATERAL_ACCEL live in a SHARED physics header
// (see §4b, in mclConfig.h) so boomerang and MCL use the exact same numbers.

// nearest perimeter wall along world heading phiRad (radians, VEX: 0 = +Y, CW+)
// from world point (sx, sy). Returns SENSOR_MAX_RANGE if nothing valid.
inline double rayToWalls(double sx, double sy, double phiRad) {
  const double H  = MCLConfig::FIELD_HALF;
  const double dx = sin(phiRad);   // +X component (VEX convention)
  const double dy = cos(phiRad);   // +Y component
  double best = MCLConfig::SENSOR_MAX_RANGE;

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
```

---

## 6. The Sensor interface (`Sensor.h`) — Echo's pattern

```cpp
#pragma once
#include <optional>

// p() = probability of the actual reading given the particle pose.
// Returns std::nullopt to ABSTAIN (out of range / no object) — skipped in the product.
class Sensor {
public:
  virtual std::optional<double> p(const Particle& particle, double headingRad) = 0;
  virtual void update() = 0;          // read the hardware ONCE per frame
  virtual ~Sensor() = default;
};
```

### Distance sensor model (in `mcl.h` / `mcl.cpp`)

```cpp
#pragma once
#include "vex.h"
#include "mcl/mcl.h"
#include "mcl/mcl.h"
#include <cmath>

class MCLDistanceSensor : public Sensor {
public:
  // Where the sensor sits and points, measured on the robot:
  //   mountForward  = inches ahead of the tracking center
  //   mountSideways = inches to the right of the tracking center
  //   mountAngleDeg = which way it faces, relative to the robot's front (VEX degrees)
  //   scaleConstant = per-sensor fix because distance sensors read ~1-3% high
  MCLDistanceSensor(vex::distance* device, double mountForward, double mountSideways,
                    double mountAngleDeg, double scaleConstant) {
    this->device        = device;
    this->mountForward  = mountForward;
    this->mountSideways = mountSideways;
    this->mountAngleDeg = mountAngleDeg;
    this->scaleConstant = scaleConstant;
  }

  // Read the hardware ONCE per frame (called once, then reused for all particles).
  void update() override {
    hasValidReading = device->isObjectDetected();

    double rawMillimeters = device->objectDistance(vex::distanceUnits::mm);
    measuredInches = scaleConstant * rawMillimeters / 25.4;   // mm -> in, with the scale fix

    if (measuredInches > MCLConfig::SENSOR_MAX_RANGE) {
      hasValidReading = false;                                // too far away to trust
    }

    // bigger reading -> bigger uncertainty (datasheet: about 2.5% of the distance)
    standardDeviation = MCLConfig::SENSOR_STD_FRAC * measuredInches;
    if (standardDeviation < MCLConfig::SENSOR_STD_FLOOR) {
      standardDeviation = MCLConfig::SENSOR_STD_FLOOR;
    }
  }

  // How well does THIS particle explain the reading we just took?
  std::optional<double> p(const Particle& particle, double headingRad) override {
    if (!hasValidReading) {
      return std::nullopt;                  // this sensor sits out the vote this frame
    }

    // 1. find the sensor's spot in the field (rotate its robot-frame mount by heading)
    double sensorX = particle.x + mountForward * sin(headingRad) + mountSideways * cos(headingRad);
    double sensorY = particle.y + mountForward * cos(headingRad) - mountSideways * sin(headingRad);

    // 2. find which way the sensor points in the field
    double sensorHeadingRad = headingRad + mountAngleDeg * (M_PI / 180.0);

    // 3. how far SHOULD it read to the wall from here?
    double expectedInches = rayToWalls(sensorX, sensorY, sensorHeadingRad);

    // 4. score the match: closer to the real reading -> higher weight
    double score = bellCurve(expectedInches, measuredInches, standardDeviation);
    return score * MCLConfig::DISTANCE_WEIGHT;
  }

private:
  vex::distance* device;
  double mountForward, mountSideways, mountAngleDeg, scaleConstant;
  double measuredInches   = 0.0;
  double standardDeviation = 1.0;
  bool   hasValidReading  = false;
};
```

*(Future `Sensor` subclasses, same interface: `MCLLineSensor` — field tape at known Y, binary soft-weight; `MCLGps` — `vex::gps` pose with σ from its error, abstain on bad status, no prediction; `MCLAprilTag` — AI Vision bearing+range to a known tag, the strongest term since the ID kills symmetry. All plug in with `addSensor()`.)*

---

## 7. ParticleFilter (in `mcl.h` / `mcl.cpp`)

```cpp
#pragma once
#include "vex.h"
#include "mcl/mcl.h"
#include "mcl/mcl.h"
#include "mcl/mclConfig.h"
#include <vector>

class ParticleFilter {
public:
  Particle estimate;            // the average particle -> this is blended into chassis

  // ---- setup --------------------------------------------------------------

  // Start the cloud as a small blob around a known pose (normal auton start).
  void seedAround(double centerX, double centerY) {
    for (int i = 0; i < N; i++) {
      particles[i].x = centerX + randomBetween(-MCLConfig::SEED_STD, MCLConfig::SEED_STD);
      particles[i].y = centerY + randomBetween(-MCLConfig::SEED_STD, MCLConfig::SEED_STD);
    }
    estimate.x = centerX;
    estimate.y = centerY;
  }

  // Scatter the cloud across the whole field (for "I'm totally lost" recovery).
  void seedUniform() {
    for (int i = 0; i < N; i++) {
      particles[i].x = randomBetween(-MCLConfig::FIELD_HALF, MCLConfig::FIELD_HALF);
      particles[i].y = randomBetween(-MCLConfig::FIELD_HALF, MCLConfig::FIELD_HALF);
    }
  }

  void addSensor(Sensor* sensor) {
    sensors.push_back(sensor);
  }

  // ---- one MCL cycle ------------------------------------------------------
  // globalDX, globalDY = how far the robot moved (field frame) since last call.
  // headingRad         = the trusted IMU heading right now.
  // slipAccel          = centripetal acceleration v^2/r right now (sideways slip risk).
  void step(double globalDX, double globalDY, double headingRad, double slipAccel) {
    moveEveryParticle(globalDX, globalDY, headingRad, slipAccel);

    distanceSinceUpdate += sqrt(globalDX * globalDX + globalDY * globalDY);

    // Only do the expensive sensor weighting after we have moved enough (or waited
    // long enough). Doing it while sitting still would collapse the cloud.
    double secondsSinceUpdate = (vex::timer::system() - lastUpdateMs) / 1000.0;
    bool movedEnough = distanceSinceUpdate >= MCLConfig::MIN_UPDATE_DIST;
    bool waitedEnough = secondsSinceUpdate >= MCLConfig::MAX_NO_UPDATE_TIME;
    if (!movedEnough && !waitedEnough) {
      updateEstimate();
      return;
    }
    if (sensors.empty()) {
      updateEstimate();
      return;
    }

    readEverySensorOnce();
    double totalWeight = weightEveryParticle(headingRad);
    if (totalWeight <= 0.0) {
      updateEstimate();              // every particle disagreed; don't resample on garbage
      return;
    }

    resample();
    updateEstimate();
    distanceSinceUpdate = 0.0;
    lastUpdateMs = vex::timer::system();
  }

private:
  static constexpr int N = MCLConfig::NUM_PARTICLES;
  Particle particles[N];
  Particle oldParticles[N];     // a copy used while resampling
  double   weights[N];
  std::vector<Sensor*> sensors;
  double   distanceSinceUpdate = 0.0;
  uint32_t lastUpdateMs = 0;

  // PREDICT --------------------------------------------------------------
  // Move every particle the way the robot moved, then add a little random noise
  // so the cloud covers many "what might have really happened" cases.
  //
  // The noise is bigger SIDEWAYS than FORWARD, because this robot has no sideways
  // tracker wheel -> we are much less sure about sideways motion. We add the noise
  // in the robot's own frame (forward / sideways) and then rotate it into the field.
  //
  // The sideways noise also GROWS when the robot is likely slipping: slipAccel is the
  // centripetal acceleration (v^2/r), so a tight fast turn adds a lot of sideways spread.
  void moveEveryParticle(double globalDX, double globalDY, double headingRad, double slipAccel) {
    double distanceMoved = sqrt(globalDX * globalDX + globalDY * globalDY);

    // Friction holds up to MAX_LATERAL_ACCEL; only the EXCESS centripetal accel makes
    // the robot actually slide. Sideways noise grows with that excess (zero below the limit).
    double excessAccel = slipAccel - RobotPhysical::MAX_LATERAL_ACCEL;
    if (excessAccel < 0.0) {
      excessAccel = 0.0;
    }
    double sidewaysFraction = MCLConfig::SIDEWAYS_NOISE + MCLConfig::SLIP_GAIN * excessAccel;

    for (int i = 0; i < N; i++) {
      // 1. random noise amounts, in the robot's frame
      double forwardNoise  = randomBetween(-1.0, 1.0) * MCLConfig::FORWARD_NOISE * distanceMoved;
      double sidewaysNoise = randomBetween(-1.0, 1.0) * sidewaysFraction         * distanceMoved;

      // 2. rotate that robot-frame noise into field X/Y
      //    (forward points along heading; sideways is 90 deg to the right)
      double noiseX = forwardNoise * sin(headingRad) + sidewaysNoise * cos(headingRad);
      double noiseY = forwardNoise * cos(headingRad) - sidewaysNoise * sin(headingRad);

      // 3. move this particle: the real robot motion + its own noise
      particles[i].x += globalDX + noiseX;
      particles[i].y += globalDY + noiseY;
    }
  }

  // UPDATE ---------------------------------------------------------------
  void readEverySensorOnce() {
    for (int s = 0; s < (int)sensors.size(); s++) {
      sensors[s]->update();
    }
  }

  // Give every particle a weight = how well it agrees with ALL the sensors.
  double weightEveryParticle(double headingRad) {
    double totalWeight = 0.0;

    for (int i = 0; i < N; i++) {
      // a particle that wandered off the field is useless; drop it somewhere random
      if (isOutsideField(particles[i])) {
        particles[i].x = randomBetween(-MCLConfig::FIELD_HALF, MCLConfig::FIELD_HALF);
        particles[i].y = randomBetween(-MCLConfig::FIELD_HALF, MCLConfig::FIELD_HALF);
      }

      // start at 1.0, then multiply in each sensor's vote
      double weight = 1.0;
      for (int s = 0; s < (int)sensors.size(); s++) {
        std::optional<double> vote = sensors[s]->p(particles[i], headingRad);
        if (vote.has_value()) {
          weight = weight * vote.value();
        }
        // if the sensor abstained (nullopt), we just skip it
      }

      weights[i] = weight;
      totalWeight += weight;
    }
    return totalWeight;
  }

  // RESAMPLE -------------------------------------------------------------
  // Build a fresh set of particles, copying heavy ones more often and dropping
  // light ones. "Systematic resampling": lay all the weights end-to-end on a line,
  // drop N equally-spaced points on it, and keep whichever particle each lands on.
  // (Kept compact on purpose: the obvious version would be slower for no real gain.)
  void resample() {
    double totalWeight = 0.0;
    for (int i = 0; i < N; i++) {
      totalWeight += weights[i];
    }
    double spacing = totalWeight / double(N);          // gap between our N points
    double firstPoint = randomBetween(0.0, spacing);   // random start offset

    for (int i = 0; i < N; i++) {
      oldParticles[i] = particles[i];                  // snapshot before overwriting
    }

    int sourceIndex = 0;
    double weightSoFar = weights[0];
    for (int newIndex = 0; newIndex < N; newIndex++) {
      double thisPoint = firstPoint + double(newIndex) * spacing;
      while (thisPoint > weightSoFar && sourceIndex < N - 1) {
        sourceIndex++;
        weightSoFar += weights[sourceIndex];
      }
      particles[newIndex] = oldParticles[sourceIndex];
    }
    // OPTIONAL roughening / random injection (INJECT_FRAC) would go here. Off by default.
  }

  // ESTIMATE -------------------------------------------------------------
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
    double H = MCLConfig::FIELD_HALF;
    return p.x > H || p.x < -H || p.y > H || p.y < -H;
  }
};
```

---

## 8. Integration with LucasLib

### 8a. Expose the increment from `track()` (minimal change to `odometry.cpp`)

`track()` already computes the global delta it adds to `chassis.x/y` (lines 154–155). Accumulate it for the MCL task to consume:

```cpp
// in Odometry: new members
double mclAccumDX = 0, mclAccumDY = 0;
vex::mutex mclMutex;

// inside track(), right where x/y are updated:
double gdx = localDY * sin(avgHRad) + localDX * cos(avgHRad);
double gdy = localDY * cos(avgHRad) - localDX * sin(avgHRad);
x += gdx;
y += gdy;
mclMutex.lock();
mclAccumDX += gdx;          // accumulate in GLOBAL frame (correct across heading changes)
mclAccumDY += gdy;
mclMutex.unlock();
```

> Accumulating in the **global** frame is what makes this correct: because heading is shared, the global delta is identical for every particle, so predict is just `particle += accumΔ + noise`.

### 8b. The MCL task (in `mcl.cpp`)

```cpp
ParticleFilter mcl;
vex::distance distFront = vex::distance(PORT1);   // etc., per robot

void StartMCL(double startX, double startY) {
  // Exactly 4 sensors. Each: (device, mountForward, mountSideways, mountAngleDeg, scale).
  // mountForward/mountSideways are BOTH measured from the tracking center (2-axis offset).
  // Example mounts (replace with your real measurements + calibrated scale constants):
  static MCLDistanceSensor frontRight(&distFR,  6.0,  5.0,   0.0, 1.00);  // faces front
  static MCLDistanceSensor frontLeft (&distFL,  6.0, -5.0,   0.0, 1.00);
  static MCLDistanceSensor backLeft  (&distBL, -6.0, -5.0, 180.0, 1.00);  // faces back
  static MCLDistanceSensor backRight (&distBR, -6.0,  5.0, 180.0, 1.00);
  mcl.addSensor(&frontRight);
  mcl.addSensor(&frontLeft);
  mcl.addSensor(&backLeft);
  mcl.addSensor(&backRight);

  mcl.seedAround(startX, startY);
}

int mclTask() {
  while (true) {
    chassis.mclMutex.lock();
    double gdx = chassis.mclAccumDX;
    double gdy = chassis.mclAccumDY;
    chassis.mclAccumDX = 0;
    chassis.mclAccumDY = 0;
    chassis.mclMutex.unlock();

    double hRad = chassis.h * (M_PI / 180.0);
    // how likely the robot is to be slipping sideways right now (turning hard at speed).
    // reuses values track() already computes: forward velocity and the arc radius.
    double slipAccel = centripetalAcceleration(chassis.velocity, chassis.lastArcRadius);
    mcl.step(gdx, gdy, hRad, slipAccel);

    // BLEND the correction into the shared pose (increment-based, not averaging)
    chassis.mclMutex.lock();
    chassis.x += MCLConfig::MCL_WEIGHT * (mcl.estimate.x - chassis.x);
    chassis.y += MCLConfig::MCL_WEIGHT * (mcl.estimate.y - chassis.y);
    chassis.mclMutex.unlock();
    // chassis.h is NEVER touched — resolveHeading owns heading.

    vex::this_thread::sleep_for(MCLConfig::TASK_DELAY_MS);
  }
  return 0;
}
```

### 8c. Wire it in (`main.cpp` / auton)

```cpp
StartMCL(-48, -60);          // known auton start
vex::task mclThread(mclTask);
// odometryTask already running; movement reads chassis.x/y/h unchanged.
```

**Movement code does not change** — `driveTo`, boomerang, etc. keep reading `chassis`, now self-correcting.

---

## 9. Threading & safety

- `odometryTask` ~100 Hz (existing) — keeps `chassis` smooth, accumulates `mclAccumDX/DY`.
- `mclTask` ~50 Hz (new) — predict/gate/weight/resample, blends correction.
- `chassis.x/y` and the accumulator are guarded by `mclMutex` (V5 doubles aren't atomic; one user core, time-sliced — the task split buys responsiveness, not parallel speedup).

---

## 10. Bring-up & tuning plan

1. **Map test** — place a particle at the true pose; print `predicted` vs `measured` per sensor. Must match within σ. Fix mounts (`rx, ry, betaDeg`) and the heading convention **first** (#1 bug source).
2. **Per-sensor tuning constant** — face a wall at known distances; each `vex::distance` reads ~1–3 % high → set `tune` to correct it.
3. **Predict-only** — disable sensors; drive a known square; cloud should follow odometry and spread.
4. **Update-only** — hold still; cloud sharpens onto the seed.
5. **Full, slow → fast**; watch `chassis.x/y` track. Tune `DRIVE_NOISE`, then `MCL_WEIGHT`.
6. **Kidnap / opponent** — push the robot; confirm recovery (enable `INJECT_FRAC` if needed) and that one blocked sensor doesn't collapse it.
7. **Ground-truth** — measure real pose vs. estimate to quantify error (Echo flags this as essential).

## 11. Sensor suite (Echo's, mapped to LucasLib)

- **3–4 `vex::distance`** at the walls (uses the four `OdometryConfig` offset slots → promote to full `(rx, ry, betaDeg)` mounts).
- **dual IMU** → `resolveHeading` (already protected; keep on dampeners).
- **Future `Sensor` subclasses** (drop-in via `addSensor`): line tracker (tape landmarks), `vex::gps` (status-gated), AI Vision AprilTag (Override) — each just implements `p()` + `update()`.

## 12. Phased rollout

- **Phase 1** — `ParticleFilter` + `MCLDistanceSensor` + the `track()` accumulator + blend. Distance-only, 4 sensors. Validate with the bring-up plan.
- **Phase 2** — tune noise/blend on the real robot; add `INJECT_FRAC` and/or heavy-tail clamp only if kidnap/defense testing demands it.
- **Phase 3** — add line and/or GPS `Sensor` subclasses for redundancy.
- **Phase 4** — AprilTag `Sensor` subclass once Override field tags are specced.

---

### One-line summary
Add a 400-particle, IMU-heading, distance-vs-walls particle filter built on Echo's pluggable-`Sensor` + distance-gated-update + systematic-resample design; feed its predict from the arc-chord increment `track()` already computes; blend its corrected (x, y) into `chassis` at α≈0.35 so every existing movement function silently rides a self-correcting pose.
