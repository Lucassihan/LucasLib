#include "auton.h"
#include "movement.h"
#include "movementUtils.h"
#include "coreConfigs.h"
#include "misc/util.h"
#include "debugger/debugger.h"
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

  //LyPosePID
  lyPosePID.setProfile(normal);
  lyPosePID.addStep(99999, 0.1, 0, 1.506); //LyPose PID shouldn't have any kI or kD. 
}

void autonSettings() {
  initializePIDConstants(); //This is necessary, we need to make sure pid.cpp gets our constants
  drivePID.maxAccelRate = 0.25;
  drivePID.maxDecelRate = 0.5;
  // totalOutput.maxAccelRate = 0.5;
  // totalOutput.maxDecelRate = 0.5;

  //You can put your own stuff in here too
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