#include "main.h"
#include "auton.h"
#include "coreConfigs.h"
#include "misc/controllerButtonMonitoring.h"
#include "misc/driverControllers.h"
#include "misc/util.h"
#include "tasks.h"
#include "debugger/debugger.h"
#include "pid.h"
#include "scheduler.h"
#include "velocityTuning.h"
#include "scoringControl/controlThread.h"
#include "scoringControl/dispatchThread.h"

#include <cmath>
#include <cstdio>

std::string currentAuton;


// Intake
motor intakeMotor = motor(PORT9, ratio18_1, true);
motor clawMotor = motor(PORT17, ratio18_1, false);
motor clawRollers = motor(PORT17, ratio18_1, false);
motor liftMotor = motor(PORT17, ratio18_1, false);

//4Bar
motor fourBarLeft = motor(PORT16, ratio36_1, false);
motor fourBarRight = motor(PORT15, ratio36_1, true);
motor_group fourBar = motor_group(fourBarLeft, fourBarRight);
rotation fourBarRotation = rotation(PORT14, false);

//Mag distance sensors
distance autoClampSens = distance(PORT21);

//Color sensor
optical OtwColorSensor = optical(PORT1);

//Pistons
digital_out zhuazi = digital_out(Brain.ThreeWirePort.A);

void intake() {
  intakeMotor.spin(forward, 100, pct);
}

void spit() {
  intakeMotor.spin(reverse, 100, pct);
}

void stop() {
  intakeMotor.stop(coast);
}

//height is 7 in
// 4bar Control Task
double rotationOffset = -228;
double L1 = 13;
double L2 = 15;
double offset1 = 15;
double offset2 = 4;
double calculateHeight() {
  double theta = TO_RAD(fourBarRotation.position(degrees) + rotationOffset);
  double height = (L1+L2)*sin(theta) + offset1 + offset2;
  return height;
}

double passiveLiftVolts = 1;
double feedforwardTerm() {
  return cos(TO_RAD(fourBarRotation.position(degrees) + rotationOffset))*passiveLiftVolts;
}

double targetLevel = 0;
double halfCupHeight = 3.5;
double targetHeight;
double currentHeight;
double error;
double final4barOutput;
bool fourBarPIDEnabled = false;
int fourBarControlTask() {
  currentHeight = calculateHeight();
  while (true) {
    if (fourBarPIDEnabled) {
      currentHeight = calculateHeight();
      targetHeight = targetLevel * halfCupHeight;
      error = targetHeight - currentHeight;

      final4barOutput = fourBarPID.calculateOutput(error) + feedforwardTerm();
      fourBar.spin(forward, final4barOutput, volt);
    }
    delay(10);
  }
  return 0;
}

void clamp() {
  zhuazi.set(false);
}

void unclamp() {
  zhuazi.set(true);
}

bool autoClampEnabled;
bool clamped;
int autoClampTask() {
  while (true) {
    if (autoClampEnabled and not clamped) {
      if (autoClampSens.objectDistance(inches) < 1) {
        clamp();
        Controller.rumble(".");
        clamped = true;
      } 
    }
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
    // velocityTuner.angular.display();
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
    Controller.Screen.print("Claw: %3.f", claw.currentAngle);

    Controller.Screen.setCursor(3, 13);
    Controller.Screen.print("Lift: %3.f", lift.currentHeight);

    delay(50);
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
  vexImuDataRateSet(IMU1.index(), 5);
  vexImuDataRateSet(IMU2.index(), 5);

  vexAbsEncDataRateSet(TX.index(), 5);
  vexAbsEncDataRateSet(TY.index(), 5);

  leftdrive.setStopping(brake);
  rightdrive.setStopping(brake);

  startTasks();

  createTrajectories();
  
  claw.setPosition(claw.clawMin);
  lift.setPosition(lTuck);

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
  // redAuto();
  // blueAuto();
  // exampleAuto();
  trajectoryTest();

  while (Competition.isAutonomous()) {
    delay(20);
  }
}

void driver() {

  fourBarPIDEnabled = false;

  fourBar.setStopping(hold);


  Controller.rumble(".");

  while (true) {
    
    // velocityTuner.angular.collectData(2, 1.0, 3.0, 0.3);
    controllerButtonMonitoringFcn();

    arcadeDriveFcn();
    // singleStickDriveFcn();

    
    delay(1);
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
