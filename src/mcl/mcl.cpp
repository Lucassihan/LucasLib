#include "mcl/mcl.h"
#include "coreConfigs.h"   // chassis, DistSensor1..4
#include "misc/util.h"     // TO_RAD, delay

// ============================================================================
// mcl.cpp -- the live filter, the 4 sensor objects, and the task loop that
// feeds the filter from chassis and blends its correction back in.
// ============================================================================

static ParticleFilter    mcl;
static MCLDistanceSensor  sensorObjects[4];
static bool mclInitialized = false;   // sensors registered?
static bool mclReady       = false;   // seeded and running?

// Build the 4 distance-sensor models once, from the config + the real devices.
static void initSensorsOnce() {
  if (mclInitialized) {
    return;
  }
  vex::distance* devices[4] = { &DistSensor1, &DistSensor2, &DistSensor3, &DistSensor4 };
  for (int i = 0; i < 4; i++) {
    sensorObjects[i].configure(devices[i],
                               MCLConfig.SENSOR_FORWARD[i],
                               MCLConfig.SENSOR_SIDEWAYS[i],
                               TO_RAD(MCLConfig.SENSOR_FACE_ANGLE[i]),  // deg -> rad once, at setup
                               MCLConfig.SENSOR_SCALE[i]);
    mcl.addSensor(&sensorObjects[i]);
  }
  mclInitialized = true;
}

// Seed the cloud around a known starting pose, and TURN MCL ON.
// Call at auton start (and again after any chassis.setPos).
void StartMCL(double startX, double startY) {
  initSensorsOnce();
  mcl.seedAround(startX, startY);
  // Discard any motion that piled up while MCL was off, so the first cycle
  // doesn't jump the cloud by everything that happened since boot / since StopMCL.
  chassis.mclMutex.lock();
  chassis.mclAccumDX = 0;
  chassis.mclAccumDY = 0;
  chassis.mclMutex.unlock();
  mclReady = true;
}

// TURN MCL OFF -> chassis goes back to pure odometry (heading was never touched).
// The task keeps running but idles; call StartMCL() again to re-enable.
void StopMCL() {
  mclReady = false;
}

// The background task: predict from chassis motion, weight against the walls,
// then blend the corrected (x, y) into chassis. Launch once from startTasks().
int mclTask() {
  while (true) {
    if (!mclReady) {
      delay(MCLConfig.TASK_DELAY_MS);
      continue;
    }

    // 1. take the motion the robot made since our last cycle (and reset it)
    chassis.mclMutex.lock();
    double globalDX = chassis.mclAccumDX;
    double globalDY = chassis.mclAccumDY;
    chassis.mclAccumDX = 0;
    chassis.mclAccumDY = 0;
    chassis.mclMutex.unlock();

    // 2. run one filter step
    double headingRad = TO_RAD(chassis.h);
    double slipAccel  = centripetalAcceleration(chassis.velocity, chassis.travelArcRadius());
    mcl.step(globalDX, globalDY, headingRad, slipAccel, vex::timer::system());

    // 3. gently blend the correction into the shared pose (heading is left alone)
    chassis.mclMutex.lock();
    chassis.x += MCLConfig.MCL_WEIGHT * (mcl.estimate.x - chassis.x);
    chassis.y += MCLConfig.MCL_WEIGHT * (mcl.estimate.y - chassis.y);
    chassis.mclMutex.unlock();

    delay(MCLConfig.TASK_DELAY_MS);
  }
  return 0;
}
