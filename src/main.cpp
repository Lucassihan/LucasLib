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
motor intakeMotor = motor(PORT21, ratio18_1, false);
motor clawMotor = motor(PORT13, ratio18_1, false);
motor clawRollers = motor(PORT20, ratio18_1, false);
motor liftMotorL = motor(PORT1, ratio18_1, false);
motor liftMotorR = motor(PORT2, ratio18_1, true);
motor_group liftMotor = motor_group(liftMotorL, liftMotorR);

//4Bar
motor fourBarLeft = motor(PORT16, ratio36_1, false);
motor fourBarRight = motor(PORT15, ratio36_1, true);
motor_group fourBar = motor_group(fourBarLeft, fourBarRight);
rotation fourBarRotation = rotation(PORT14, false);

//Mag distance sensors
distance autoClampSensor = distance(PORT21);

//Color sensor
optical OtwColorSensor = optical(PORT1);

//Pistons
digital_out zhuazi = digital_out(Brain.ThreeWirePort.A);

bool intaking;
void intake() {
  intakeMotor.spin(forward, 100, pct);
  intaking = true;
}

bool spitting;
void spit() {
  intakeMotor.spin(reverse, 100, pct);
  spitting = true;
}

void stop() {
  intakeMotor.stop(coast);
  intaking = false;
  spitting = false;
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
    Controller.Screen.print("H:%.3f", chassis.h);

    Controller.Screen.setCursor(1, 10);
    Controller.Screen.print("Chassis:%.2f", chassisTempAvg);

    Controller.Screen.setCursor(2, 10);
    Controller.Screen.print("Claw: %.3f", claw.currentAngle);

    Controller.Screen.setCursor(3, 10);
    Controller.Screen.print("Lift: %.3f", lift.currentHeight);

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

    vex::this_thread::sleep_for(100);
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

  initializeClawPIDConstants();

  startTasks();

  createTrajectories();
  
  initializePIDConstants();

  claw.setPosition(claw.clawMin);
  lift.setPosition(0);

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

  claw.asyncControl = true;
  lift.asyncControl = true;

  Controller.rumble(".");

  claw.setPosition(-56.6);
  lift.setPosition(0);
  clawAction(Tuck);

  bool clawMode = false;

  while (true) {
    controllerButtonMonitoringFcn();

    bool spitCombo = false;

///////////////////////////////////////////////
//  | |         / _| |       (_)   | |     
//  | |     ___| |_| |_   ___ _  __| | ___ 
//  | |    / _ \  _| __| / __| |/ _` |/ _ \
//  | |___|  __/ | | |_  \__ \ | (_| |  __/
//  |______\___|_|  \__| |___/_|\__,_|\___|
///////////////////////////////////////////////

    if (l1.NewPress) {
      if (clawCurrentState == Open) {
        clawAction(Drop);
      } else {
        clawAction(Open);

      }
    }

    if (l2.Pressing) {
      if (clawCurrentState != Tuck) {
        clawAction(Tuck);
      }
      if (clawCurrentState == Tuck and not l1.pressedSecond) {
        intake();
      }
    }

    if (l2.pressedFirst and l1.pressedSecond) {
      spitCombo = true;
      spit();
      claw.spit(12);
    }

    if (not l2.Pressing) {
      stop();
    }

////////////////////////////////////////////////////
//   _____  _       _     _         _     _      
//  |  __ \(_)     | |   | |       (_)   | |     
//  | |__) |_  __ _| |__ | |_   ___ _  __| | ___ 
//  |  _  /| |/ _` | '_ \| __| / __| |/ _` |/ _ \
//  | | \ \| | (_| | | | | |_  \__ \ | (_| |  __/
//  |_|  \_\_|\__, |_| |_|\__| |___/_|\__,_|\___|
//             __/ |                             
//            |___/                              
////////////////////////////////////////////////////

    if (r1.Pressing) {
      if (clawCurrentState == Tuck or clawCurrentState == Drop) {
        clawAction(LeaveChamber);
      }

      if (clawCurrentState == LeaveChamber) {
        liftPID.minOutput = 12;
        lift.move(lift.currentHeight + 2);
      }
    }

    if (r2.Pressing) {
      if (clawCurrentState == LeaveChamber) {
        liftPID.minOutput = 12;
        lift.move(lift.currentHeight - 2);
      }

      if (r1.pressedSecond)  {
        clawAction(Score);
      }
    }

    if (not r2.Pressing and not r1.Pressing and finished and clawCurrentState != Tuck) {
      liftPID.minOutput = 0;
      lift.move(lift.currentHeight);
    }

/////////////////////////////////////////////////////////////////////
//   __  __ _            _    _           _       _   _             
//  |  \/  (_)          | |  | |         | |     | | (_)            
//  | \  / |_ ___  ___  | |  | |_ __   __| | __ _| |_ _ _ __   __ _ 
//  | |\/| | / __|/ __| | |  | | '_ \ / _` |/ _` | __| | '_ \ / _` |
//  | |  | | \__ \ (__  | |__| | |_) | (_| | (_| | |_| | | | | (_| |
//  |_|  |_|_|___/\___|  \____/| .__/ \__,_|\__,_|\__|_|_| |_|\__, |
//                             | |                             __/ |
//                             |_|                            |___/ 
/////////////////////////////////////////////////////////////////////

    if (not overrideClawRollerControl and not spitCombo) {
      if (clawCurrentState == Tuck and intaking) {
        claw.intake(12);
      }

      if (clawCurrentState == Tuck and not intaking) {
        claw.stop();
      }

      if (clawCurrentState == Open or clawCurrentState == Drop) {
        claw.intake(12);
      }

      if (clawCurrentState == LeaveChamber) {
        claw.intake(6);
      }

      if (not (clawCurrentState == Tuck or clawCurrentState == Open)) {
        claw.kFeedForward = 1.7 + 2;
        lift.kFeedForward = 1.5 + 0.5;
      } else {
        claw.kFeedForward = 1.7;
        lift.kFeedForward = 1.5;
      }
    }

    arcadeDriveFcn();

    
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
