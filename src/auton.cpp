#include "auton.h"
#include "movement.h"
#include "movementUtils.h"
#include "coreConfigs.h"
#include "misc/util.h"
#include "debugger/debugger.h"
#include "ramsete/ramsete.h"
#include "pid.h"
#include "main.h"
#include "scheduler.h"

#include <cmath>

//Declare PID constants
void initializePIDConstants() {
  //TurnPID
  turnPID.setProfile(normal);
  turnPID.addStep(45, 0.1, 0.001, 3.644);
  turnPID.addStep(90, 0.12, 0.001, 3.744);
  turnPID.addStep(180, 0.15, 0.001, 3.844);
  turnPID.setIntegralSettings(10, 3);

  //SwingPID
  swingPID.setProfile(normal);
  swingPID.addStep(999999, 0.1, 0.001, 1.506);
  swingPID.setIntegralSettings(10, 3);

  //DrivePID
  drivePID.setProfile(normal);
  drivePID.addStep(99999, 0.5, 0., 9);
  drivePID.setIntegralSettings(2, 3);

  //HeadingPID
  headingPID.setProfile(normal);
  headingPID.addStep(999999, 0.1, 0, 1.506); //Do not give headingPID any ki

}

void autonSettings() {
  initializePIDConstants(); //This is necessary, we need to make sure pid.cpp gets our constants
  setRamseteSettings();                    //the law's gains + the chassis limits
  initializeVelocityControllerConstants(); //what each channel measures, and its gains

  totalOutput.maxOutput = 12;

  //You can put your own stuff in here too
}

void exampleAuto() {
  debugger::start("exampleauto");
  autonSettings();

  chassis.setPos(0,0,0);
  moveTo(0,24, {.d = 1});
  BoomParams bp {.x = 24, .y = 48, .lead = 0.5};
  facePoint(bp, {.d = 10});
  drivePID.maxOutput = 6;
  boomerangTo(bp, {.d = 3});
  debugger::stop();

}

void trajectoryTest() {
  debugger::start("secondTrajTest");
  autonSettings();
  chassis.setPos(48,-48,0);
  std::vector<BezierSegment> test = {
    {.a = {48, -48}, .b = {48, -42.1}, .c = {-74.1, 0.1}, .d = {0, 0}},
    {.a = {0, 0}, .b = {48, -0.1}, .c = {-30, 48}, .d = {0, 48}}
  };
  ramsete.trajectory(bakeTrajectory(test, {.maxVel = 75.0, .maxAccel = 100.0}));
  debugger::stop();
}


bool cond1() {
  if (chassis.distanceTo(24,24) < 2) {
    return true;
  } else {
    return false;
  }
}

void exampleAuton() {
  debugger::start("example");
  autonSettings();
  chassis.setPos(0,0,0);

  schedule(cond1, scoreFcn);

  debugger::stop();
}

void blueAuto() {
  fourBar.spin(forward, -12, volt);
  voldrive(2);
  delay(500);
  fourBar.spin(forward, 12, volt);
  delay(500);
  voldrive(-1);
  fourBar.stop(coast);
  delay(500);
  fourBar.spin(forward, -12, volt);
  delay(500);

  voldrive(-2);
  delay(1000);
  voldrive(0);
}

void redAuto() {
  fourBar.spin(forward, -12, volt);
  voldrive(2);
  delay(500);
  fourBar.spin(forward, 12, volt);
  delay(500);
  voldrive(-1);
  fourBar.stop(coast);
  delay(500);
  fourBar.spin(forward, -12, volt);
  delay(500);

  voldrive(3);
  delay(500);
  voldrive(0);
  fourBar.spin(forward, 12, volt);
  delay(500);
  fourBar.stop(coast);
  voldrive(-1);
  delay(1000);
  voldrive(0);
}