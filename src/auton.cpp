#include "auton.h"
#include "movement.h"
#include "coreConfigs.h"
#include "misc/util.h"

#include <cmath>

//Declare PID constants
void initializePIDConstants() {
  //TurnPID
  turnPID.setProfile(normal);
  turnPID.addStep(999999, 0.1, 0.001, 1.506);
  turnPID.setIntegralSettings(10, 3);

  //SwingPID
  swingPID.setProfile(normal);
  swingPID.addStep(999999, 0.1, 0.001, 1.506);
  swingPID.setIntegralSettings(10, 3);

  //DrivePID
  drivePID.setProfile(normal);
  drivePID.addStep(99999, 1, 0.01, 21.083);
  drivePID.setIntegralSettings(2, 3);

  //HeadingPID
  headingPID.setProfile(normal);
  headingPID.addStep(999999, 0.1, 0, 1.506); //Do not give headingPID any ki

  //LyPosePID
  lyPosePID.setProfile(normal);
  lyPosePID.addStep(99999, 0.01, 0, 0); //LyPose PID shouldn't have any kI or kD. 
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
  autonSettings();
  chassis.setPos(0,0,90);

//   moveTo(24,24, {.timeout = 50000});

  // facePoint(24, 24, {.exitPct = 0.8});
  // moveTo(24, 24, {.settle = true});
  // facePoint(0, 24, {.exitPct = 0.8}, true);
  // moveTo(0, 24, {.settle = true},true);
  // facePoint(0,0,{.exitPct = 0.8}, true);
  // moveTo(0,0,{.timeout = 500000}, true);

    drivePID.maxOutput = 5;
    moveTo(16, -12, {.settle = true});
    swingOnRight(180, {.exitPct = 0.3}, 0.5);
    swingOnRight(180, {.timeout = 700});
    driveBackward(5, {.exitDist = 2});
    swingOnRightToPoint(24, 0, {.exitPct = 0.5}, false, 0);
    moveTo(24, 0, {.settle = true});

//   LateralExitConditions yFast = {
//     .exitDist = 3
//   };

//   TurnExitConditions tFast = {
//     .exitPct = 0.9
//   };


//   facePoint(10, 36, tFast);
//   moveTo(0, 36, {.timeout = 500000});
//   voltageDrive(0,0);
//   delay(100);
}

