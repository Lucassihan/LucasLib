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

#include <cmath>
#include <cstdio>

std::string currentAuton;


// Intake
motor intakeMotor = motor(PORT9, ratio18_1, true);
motor scoopMotor = motor(PORT17, ratio18_1, false);

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

bool scoop;
double lowSpeedGate = 200;
double secondGate = 200;
double limitAng = 600;

double lastAng;
double currentAng;
int scoopTask() {
  while (true) {
    if (scoop) {
      while (scoopMotor.position(degrees) < lowSpeedGate) {
        scoopMotor.spin(forward,3, volt);
        delay(10);
      }

      scoopMotor.stop(hold);
      delay(1000);

      while (scoopMotor.position(degrees) < secondGate) {
        scoopMotor.spin(forward,3, volt);
        delay(10);
      }

      while (scoopMotor.position(degrees) < limitAng) {
        scoopMotor.spin(forward, 12, volt);
        fourBar.spin(forward, 5, volt);
        delay(10);

      }

      while (scoopMotor.position(degrees) > 0) {
        currentAng = scoopMotor.position(degrees);
        scoopMotor.spin(reverse, 12, volt);
        delay(20);
        if (fabs(lastAng-currentAng) < 0.1 and scoopMotor.position(degrees) < 20) {
          break;
        } 
        lastAng = currentAng;
      }

      delay(100);
      scoopMotor.spin(forward, 0, volt);
      

      scoop = false;
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
    Controller.Screen.print("Rot: %2.f", fourBarRotation.position(degrees));

    Controller.Screen.setCursor(3, 13);
    Controller.Screen.print("H: %2.f", feedforwardTerm());

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

  // Sensor data-rate setup for the 1 ms odometry loop (delay(1) in odometryTask).
  //
  // The V5 IMU and rotation sensors round any requested data rate to a multiple of 5 ms, so
  // 5 ms is the hardware floor -- there is no way to request 1 ms. We ask every tracking
  // sensor for that fastest rate. The 1 ms loop then just oversamples each sensor ~5x and
  // sees a zero delta on the in-between ticks, which is harmless for integration.
  //
  // Ports come from the coreConfigs device objects via .index() (the same 0-based port value
  // the device was constructed with, which is exactly what the vex*DataRateSet calls expect),
  // so these can never drift out of sync with the port assignments in coreConfigs. Only the
  // sensors you actually use need a line; setting one on an empty port is a harmless no-op.
  int kSensorDataRateMs = 5;  // V5 IMU / rotation-sensor floor (requests round up to multiples of 5 ms)

  vexImuDataRateSet(IMU1.index(), kSensorDataRateMs);
  vexImuDataRateSet(IMU2.index(), kSensorDataRateMs);

  vexAbsEncDataRateSet(TX.index(), kSensorDataRateMs);
  vexAbsEncDataRateSet(TY.index(), kSensorDataRateMs);

  leftdrive.setStopping(brake);
  rightdrive.setStopping(brake);

  startTasks();
}

competition Competition;

void pre_auton() {
  initialize();
  fourBarPID.setProfile(normal);
  fourBarPID.addStep(999999, 0.1, 0.001, 3.644);
  fourBarPID.setIntegralSettings(10, 3);
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

  scoopMotor.setStopping(coast);

  Controller.rumble(".");

  while (true) {
    // velocityTuner.angular.collectData(2, 1.0, 3.0, 0.3);
    controllerButtonMonitoringFcn();

    arcadeDriveFcn();
    // singleStickDriveFcn();

    if (r1.pressedFirst) {
      targetLevel = 0;
      intake();
      fourBarPIDEnabled = true;
      targetHeight = 0;
      if (r2.pressedSecond) {
        scoop = true;
      }
    }

    if (r2.pressedFirst) {
      spit();
    }

    if (not r1.Pressing and not r2.Pressing and not scoop) {
      stop();
    }

    if (scoop) {
      intakeMotor.spin(forward, 3, volt);
    }

    if (l1.Pressing) {
      autoClampEnabled = true;
    }

    if (l1.NewPress) {
      clamped = false;
    }
    
    if (l1.JustReleased) {
      clamp();
      autoClampEnabled = false;
    }

    if (l2.NewPress) {
      unclamp();
    }

    if (scoopMotor.position(degrees) < 0) {
      scoopMotor.setPosition(0, degrees);
    }

    double powScale = 1.3;
    if (Controller.Axis2.position(pct) > 0) {
      // fourBar.spin(forward, (Controller.Axis2.position(pct)/100)*12 + feedforwardTerm(), volt);
      fourBar.spin(forward, 12, volt);
    }

    if (Controller.Axis2.position(pct) < 0) {
      fourBar.spin(forward, -2, volt);
    }

    if (Controller.Axis2.position(pct) == 0) {
      fourBar.spin(forward, feedforwardTerm(), volt);
    }
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
