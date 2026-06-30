#include "main.h"
#include "auton.h"
#include "coreConfigs.h"
#include "misc/controllerButtonMonitoring.h"
#include "misc/driverControllers.h"
#include "misc/util.h"
#include "tasks.h"
#include "debugger/debugger.h"
#include "pid.h"

#include <cmath>
#include <cstdio>

std::string currentAuton;

// Intake
motor intakeRoller = motor(PORT9, ratio18_1, false);
motor scoopMotor = motor(PORT10, ratio18_1, false);

//4Bar
motor fourBarLeft = motor(PORT10, ratio36_1, false);
motor fourBarRight = motor(PORT12, ratio36_1, true);
motor_group fourBar = motor_group(fourBarLeft, fourBarRight);
rotation fourBarRotation = rotation(PORT5, false);

//Mag distance sensors
distance autoClampSens = distance(PORT8);

//Color sensor
optical OtwColorSensor = optical(PORT1);

//Pistons
digital_out zhuazi = digital_out(Brain.ThreeWirePort.G);

void intake() {
  intakeRoller.spin(forward, 100, pct);
}

void spit() {
  intakeRoller.spin(reverse, 100, pct);
}

void stop() {
  intakeRoller.stop(coast);
}

//height is 7 in
// 4bar Control Task
double rotationOffset = 0;
double L1 = 5;
double L2 = 5;
double offset1 = 0;
double offset2 = 0;
double calculateHeight(double rotation) {
  double theta = TO_RAD(rotation + rotationOffset);
  double height = (L1+L2)*sin(theta) + offset1 + offset2;
  return height;
}

double targetLevel = 0;
double halfCupHeight = 3.5;
double targetHeight;
double currentHeight;
double error;
bool fourBarPIDEnabled = false;
int fourBarControlTask() {
  while (true) {
    if (fourBarPIDEnabled) {
      currentHeight = calculateHeight(fourBarRotation.position(degrees));
      targetHeight = targetLevel * halfCupHeight;
      error = currentHeight - targetHeight;

      fourBarPID.calculateOutput(error);
      fourBar.spin(forward, fourBarPID.output, volt);

      delay(10);
    }
  }
  return 0;
}

void clamp() {
  zhuazi.set(true);
}

void unclamp() {
  zhuazi.set(false);
}

bool autoClampEnabled;

int autoClampTask() {
  while (true) {
    if (autoClampEnabled) {
      if (autoClampSens.objectDistance(inches) < 1) {
        clamp();
        Controller.rumble(".");
      } 
    }
  }
  return 0;
}

bool scoop;
int scoopTask() {
  if (scoop) {
    scoopMotor.spin(forward,5, volt);
    delay(500);
    scoopMotor.spin(reverse,5, volt);
    scoop = false;
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
    Controller.Screen.print("None");

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

  // Drive-motor encoders (used for Y when there is no Y tracker) have NO user-settable data
  // rate -- V5 smart motors stream position over the smart port at a fixed system rate, and
  // the SDK exposes no vexMotorDataRateSet, so there is nothing to configure for them here.

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
  exampleAuton();

  while (Competition.isAutonomous()) {
    delay(20);
  }
}

// rotation TY = rotation(11, false);
motor testMotor = motor(PORT1, ratio6_1, false);

void driver() {
  double lastPos = TY.position(degrees);

  int row = 2;

  double lastChangeTime = Brain.Timer.systemHighResolution();
  testMotor.spin(forward,12,volt);

  delay(1000);

  double currentPos;

  fourBarPIDEnabled = true;

  fourBar.setStopping(hold);

  while (true) {    
    arcadeDriveFcn();
    // singleStickDriveFcn();

    if (r1.Pressed_first) {
      targetLevel = 0;
      intake();
      fourBarPIDEnabled = true;
      targetHeight = 0;
      if (r2.Pressed_second) {
        scoop = true;
      }
    }
    
    if (r2.Pressed_first) {
      spit();
    }

    if (not r1.Pressing and not r2.Pressing and not scoop) {
      stop();
    }

    if (scoop) {
      intake();
    }

    if (l1.Pressing) {
      autoClampEnabled = true;
    }
    
    if (l1.JustReleased) {
      clamp();
      autoClampEnabled = false;
    }

    if (l2.NewPress) {
      unclamp();
    }

    if (Controller.Axis2.position(pct) != 0) {
      double joypos = Controller.Axis2.position(pct);
      fourBarPIDEnabled = false;
      fourBar.spin(forward, (joypos/100)*12, volt);
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
