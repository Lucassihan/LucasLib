#include "auton.h"
#include "movement.h"
#include "coreConfigs.h"
#include "misc/util.h"
#include "debugger/debugger.h"
#include "pid.h"

#include <cmath>

//Declare PID constants
void initializePIDConstants() {
  //TurnPID
  turnPID.setProfile(normal);
  turnPID.addStep(999999, 0.1, 0.001, 3.644);
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

void exampleAuton() {
  debugger::start("example");
  autonSettings();
  chassis.setPos(0,0,0);

//   moveTo(24,24, {.timeout = 50000});

  // facePoint(24, 24, {.exitAng = 10});
  // moveTo(24, 24, {.settle = true});
  // facePoint(0, 24, {.exitAng = 10}, true);
  // moveTo(0, 24, {.settle = true},true);
  // facePoint(0,0,{.exitAng = 10}, true);
  // // moveTo(0,0,{.timeout = 500000}, true);
  // drivePID.maxOutput = 5;
  // BoomParams boom1 {
  //   .x = 24,
  //   .y = 12,
  //   .heading = 90,
  //   .lead = 0.6,
  //   .gLead = 0.4,
  // };
  // facePoint(getBoomLeadX(boom1, false), getBoomLeadY(boom1, false), {}, false);
  // boomerangTo(boom1, {.exitDist = 0}, false);

  // face(180);
  // moveTo(0, 48, {}, true);

  // totalOutput.maxOutput = 3;
  // kLat = 0.05;
  // lyPose(24, 24, {.exitDist = 0}, false, 45);

  face(180);

  // face(180);
  // // lyPose(24,12, {}, false, 90);

  debugger::stop();

//   LateralExitConditions yFast = {
//     .exitDist = 3
//   };

//   TurnExitConditions tFast = {
//     .exitAng = 5
//   };


//   facePoint(10, 36, tFast);
//   moveTo(0, 36, {.timeout = 500000});
//   voltageDrive(0,0);
//   delay(100);
}

