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
#include "scoringControl/dispatchThread.h"

#include <cmath>

//Declare PID constants
void initializePIDConstants() {
  //TurnPID
  turnPID.maxOutput = 12;
  turnPID.setProfile(normal);
  turnPID.addStep(999999, 0.15, 0.01, 4);
  turnPID.setIntegralSettings(10, 3);

  //SwingPID
  swingPID.maxOutput = 12;
  swingPID.setProfile(normal);
  swingPID.addStep(999999, 0.1, 0.001, 1.506);
  swingPID.setIntegralSettings(10, 3);

  //DrivePID
  drivePID.maxOutput = 12;
  drivePID.setProfile(normal);
  drivePID.addStep(99999, 0.6, 0.1, 6);
  drivePID.setIntegralSettings(2, 3);

  //HeadingPID
  headingPID.maxOutput = 12;
  headingPID.setProfile(normal);
  headingPID.addStep(999999, 0.15, 0, 4);
  drivePID.setIntegralSettings(2, 3);
}

void initializeClawPIDConstants() {
  //ClawPID
  clawPID.maxOutput = 12;
  clawPID.setProfile(normal);
  clawPID.addStep(999999, 0.5, 0.01, 2);
  clawPID.setIntegralSettings(45, 4);

  clawPID.setProfile(holding);
  clawPID.addStep(999999, 0.5, 0.01, 2);
  clawPID.setIntegralSettings(10, 3);

  //LiftPID
  liftPID.maxOutput = 12;
  liftPID.setProfile(normal);
  liftPID.addStep(999999, 2, 0.01, 0);
  liftPID.setIntegralSettings(10, 3);

  liftPID.setProfile(holding);
  liftPID.addStep(999999, 2, 0.001, 0);
  liftPID.setIntegralSettings(10, 3);

}

Trajectory goTraj;
Trajectory loadTraj;

void createTrajectories() {
  std::vector<BezierSegment> go = {
    {.a = {60, -60}, .b = {60, -42}, .c = {38, -47.6}, .d = {31, -47.6}}
  };
  std::vector<BezierSegment> load = {
    {.a = {31, -47.6}, .b = {38, -47.6}, .c = {60, -42}, .d = {60, -60}}
  };

  PathSettings pathSettings = {.maxVel = 60.0, .maxAccel = 100.0, .endSpeed = 0, .finalDistance = 4};

  goTraj   = createTrajectory(go, pathSettings, true);
  loadTraj = createTrajectory(load, pathSettings);
}

void autonSettings() {
  initializePIDConstants(); //This is necessary, we need to make sure pid.cpp gets our constants
  setRamseteSettings();                    //the law's gains + the chassis limits
  initializeVelocityControllerConstants(); //what each channel measures, and its gains

  claw.asyncControl = true;
  lift.asyncControl = true;
}

void exampleAuto() {
  debugger::start("exampleauto");
  autonSettings();

  chassis.setPos(0,0,0);
  // driveForward(24);
  // face(90, {.t = 50000});
  facePoint(12,24, {.v = 1});
  ramsete.moveTo(12,24,{.t = 5000});
  debugger::stop();

}

void once() {
  ramsete.trajectory(goTraj, {.d = 0});
  delay(250);
  ramsete.trajectory(loadTraj, {.v = 2});
  delay(250);
  ramsete.trajectory(goTraj, {.d = 0});
  delay(250);
  ramsete.trajectory(loadTraj, {.v = 2});
}

void tuneClaw() {

}

void trajectoryTest() {
  debugger::start("scndmacroTest");
  autonSettings();
  chassis.setPos(60,-55,180);
  backward = true;


  once();
  once();



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
  chassis.setPos(0,0,0);

  schedule(cond1, scoreFcn);

  debugger::stop();
}