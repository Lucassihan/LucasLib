#include "main.h"
#include "auton.h"
#include "coreConfigs.h"
#include "misc/controllerButtonMonitoring.h"
#include "misc/driverControllers.h"
#include "misc/util.h"
#include "tasks.h"
#include "debugger/debugger.h"

#include <cmath>
#include <cstdio>

std::string currentAuton;

// Rollers
motor intakeRollers = motor(PORT12, ratio6_1, true);
motor upperRollers = motor(PORT11, ratio6_1, false);

//Mag distance sensors
distance OtwDistSensor = distance(PORT8);
distance ChamberDistSensor = distance(PORT15);

//Color sensor
optical OtwColorSensor = optical(PORT1);

//Pistons
digital_out hood = digital_out(Brain.ThreeWirePort.G);
digital_out hook = digital_out(Brain.ThreeWirePort.A);
digital_out matchloader = digital_out(Brain.ThreeWirePort.D);
digital_out midDescore = digital_out(Brain.ThreeWirePort.H);
digital_out intakeLift = digital_out(Brain.ThreeWirePort.B);
digital_out odomLift = digital_out(Brain.ThreeWirePort.F);
digital_out midGoalHook = digital_out(Brain.ThreeWirePort.E);

// 4bar Control Task
int fourBarControl() {
  while (true) {

    delay(10);
  }
  return 0;
}

int trackDisplayTask() {
  while (true) {
    const double wheelDiameter = 3.25;
    const double wheelCircumference = wheelDiameter * M_PI;
    const double gearRatio = 36.0 / 48.0;

    double motorDegrees = mr.position(vex::deg);
    double wheelDegrees = motorDegrees * gearRatio;
    double inchesTracked = (wheelDegrees / 360.0) * wheelCircumference;

    Controller.Screen.clearScreen();
    Controller.Screen.setCursor(2, 10);
    Controller.Screen.print("MR Inches: %.3f", inchesTracked);

    vex::this_thread::sleep_for(50);
  }
  return 0;
}

int controllerScreenTask() {
  while (true) {
    if (debugger::screensMuted()) { vex::this_thread::sleep_for(40); continue; }  // clean console during a trace dump
    float leftMotorTempAvg = (
      fl.temperature(celsius) +
      ml.temperature(celsius) +
      bl.temperature(celsius)
    ) / 3.0;

    float rightMotorTempAvg = (
      fr.temperature(celsius) +
      mr.temperature(celsius) +
      br.temperature(celsius)
    ) / 3.0;

    float chassisTempAvg = (leftMotorTempAvg + rightMotorTempAvg) / 2.0;

    Controller.Screen.clearScreen();

    Controller.Screen.setCursor(1, 1);
    Controller.Screen.print("X:%.3f", chassis.x);

    Controller.Screen.setCursor(2, 1);
    Controller.Screen.print("Y:%.3f", chassis.y);

    Controller.Screen.setCursor(3, 1);
    Controller.Screen.print("H: %.3f", chassis.h);

    Controller.Screen.setCursor(1, 10);
    Controller.Screen.print("Chassis:%2.f", chassisTempAvg);

    Controller.Screen.setCursor(2, 10);
    Controller.Screen.print("Lower: %.3f", intakeRollers.temperature(celsius));

    Controller.Screen.setCursor(3, 13);
    Controller.Screen.print(currentAuton.c_str());

    vex::this_thread::sleep_for(100);
  }
  return 0;
}

bool printingDistances = true;
int brainScreenTaskFcn() {
  Brain.Screen.setFont(mono20);

  while (true) {
    if (debugger::screensMuted()) { vex::this_thread::sleep_for(40); continue; }  // clean console during a trace dump
    if (printingDistances) {
      Brain.Screen.printAt(5,   20, "D1: %.5f   ", DistSensor1.objectDistance(inches));
      Brain.Screen.printAt(350, 20, "D2: %.5f   ", DistSensor2.objectDistance(inches));
      Brain.Screen.printAt(5,  235, "D3: %.5f   ", DistSensor3.objectDistance(inches));
      Brain.Screen.printAt(350,235, "D4: %.5f   ", DistSensor4.objectDistance(inches));
    }

    vex::this_thread::sleep_for(50);
  }
  return 0;
}

void initialize() {
  vexImuDataRateSet(PORT4, 5);
  vexAbsEncDataRateSet(PORT6, 5);
  vexAbsEncDataRateSet(PORT21, 5);

  startTasks();
}

competition Competition;

void pre_auton() {
  initialize();
}

bool didAuton = false;
int autonIndex = 3;

void autonomous() {
  didAuton = true;

  // Call your auton routine here, e.g. twoMidGoals();
  exampleAuton();

  while (Competition.isAutonomous()) {
    wait(20, msec);
  }
}

void driver() {

  if (didAuton) {

  } else {

  }

  //The actual LOOP is here. Everything above this will only be run ONCE
  while (true) {

    delay(10);
  }
}

int main() {
  Competition.autonomous(autonomous);
  Competition.drivercontrol(driver);

  pre_auton();

  while (true) {
    wait(100, msec);
  }
}
