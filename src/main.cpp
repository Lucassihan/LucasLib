#include "main.h"
#include "auton.h"
#include "coreConfigs.h"
#include "misc/controllerButtonMonitoring.h"
#include "misc/driverControllers.h"
#include "misc/util.h"
#include "tasks.h"

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

//Timers
timer R2Time;

bool midDescoreIsUp;

void midDescoreUp() {
  midDescore.set(1);
  midDescoreIsUp = true;
}

void midDescoreDown() {
  midDescore.set(0);
  midDescoreIsUp = false;
}

void midDescoreToggle() {
  if (midDescoreIsUp) {
    midDescoreDown();
  } else {
    midDescoreUp();
  }
}

void hookUp() {
  hook.set(1);
}

void hookDown() {
  hook.set(0);
}

void hoodUp() {
  hood.set(1);
}

void hoodDown() {
  hood.set(0);
}

bool matchloaderIsDown = false;

void matchloaderUp() {
  matchloader.set(0);
  matchloaderIsDown = false;
}

void matchloaderDown() {
  matchloader.set(1);
  matchloaderIsDown = true;
}

void liftOdom() {
  odomLift.set(0);
}

void dropOdom() {
  odomLift.set(1);
}

bool fourBarUp = false;

void raise4bar() {
  intakeLift.set(0);
  fourBarUp = true;
}

void drop4bar() {
  intakeLift.set(1);
  fourBarUp = false;
}

bool midGoalHookIsOut = false;

void extendMidGoalHook() {
  midGoalHook.set(1);
  midGoalHookIsOut = true;
}

void retractMidGoalHook() {
  midGoalHook.set(0);
  midGoalHookIsOut = false;
}

void coastAllRollers() {
  intakeRollers.stop(coast);
  upperRollers.stop(coast);
}

void intakeFcn() {
  intakeRollers.spin(forward, 100, percent);
  upperRollers.spin(forward, 100, percent);
}

void scoreFcn() {
  intakeRollers.spin(forward, 100, percent);
  upperRollers.spin(forward, 100, percent);
}

void spitFcn() {
  intakeRollers.spin(reverse, 100, percent);
  upperRollers.spin(reverse, 100, percent);
}

double lowGoalSpeed = 20;
void lowGoalFcn(bool suspendUpper) {
  intakeRollers.spin(reverse, lowGoalSpeed, percent);
  if (suspendUpper) {
    upperRollers.stop(coast);
  } else {
    upperRollers.spin(reverse, 100, percent);
  }
}

//Intake monitor variables
bool blockOtw = false;

bool readingRed = false;
bool readingBlue = false;

bool blockOtwIsRed = false;
bool blockOtwIsBlue = false;

//Color sensor threshold constants
double redThresholdUpper = 355;
double redThresholdLower = 330;
double blueThresholdUpper = 260;
double blueThresholdLower = 200;

//Distance sensor threshold
double magDistanceThreshold = 2;
double chamberDistanceThreshold = 1.5;

//Reverse amount for color sort
double reverseTimeLongSort = 300;
double reverseTimeMidSort = 500;

//Chamber purge time
double chamberPurgeTime = 200;

//Purge oppo colors during store
bool purgingDuringStore = true;

//Intake control
IntakeMode intakeMode = coasting;

//Color sort toggles
ColorSortMode colorSortMode = none;

//Mid goal speed
int midGoalSpeed = 100;

//Low goal delay
double lowGoalIn = 0;

//Anti jam
bool intakeAntiJamEnabled = true;

//Delayed goal prime
double startPrimingAfter = 0;
bool startPrimingLowGoal = false;
double spitIn = 0;

bool suspendUpperRollers = false;
bool slowLongGoal = false;

bool wasJustIntaking = false;

double score3Time = 500;

bool firstLongGoal = false;

bool intakeLag = false;

bool autonSort = false;

bool intakeSort = false;

bool sawBadColor = false;

double slowMidSpeed = 35;

bool antiJam = true;

bool justDropped4Bar = false;

bool suspendLower = false;

// Intake Control Task
int magControlFcn() {
  while (true) {
    switch (intakeMode) {
      case (intake):
        intakeFcn();
        wasJustIntaking = true;

        if (intakeSort) {
          if (colorSortMode == teamRedColorSort && readingBlue && OtwColorSensor.isNearObject()) {
            hoodUp();
          } else if (colorSortMode == teamBlueColorSort && readingRed && OtwColorSensor.isNearObject()) {
            hoodUp();
          } else {
            hoodDown();
          }
        }
        if (antiJam) {
          if (upperRollers.velocity(pct) < 5) {
            intakeRollers.spin(forward, 100, percent);
            upperRollers.spin(reverse, 100, percent);
            delay(100);
            upperRollers.spin(forward, 100, percent);
          }

          upperRollers.spin(forward, 100, percent);
          intakeRollers.spin(forward, 100, percent);
          delay(300);

          if (intakeRollers.velocity(pct) < 3) {
            upperRollers.spin(forward, 100, percent);
            intakeRollers.spin(reverse, 100, percent);
            delay(100);
            intakeRollers.spin(forward, 100, percent);
          }
        }

        sawBadColor = false;

        break;

      case (spit):
        spitFcn();

        break;

      case (lowGoal):
        if (suspendUpperRollers) {
          lowGoalFcn(true);
        } else {
          lowGoalFcn(false);
        }

        upperRollers.spin(reverse, 100, percent);
        intakeRollers.spin(reverse, lowGoalSpeed, percent);

        break;

      case (coasting):
        coastAllRollers();
        if (wasJustIntaking && intakeLag) {
          upperRollers.spin(forward, 100, percent);
          delay(250);
        }
        wasJustIntaking = false;

        break;

      case (longGoal):
        if (not suspendLower) {
          scoreFcn();
        } else if (suspendLower) {
          upperRollers.spin(forward, 100, percent);
          intakeRollers.stop(brake);
        }

        if (autonSort) {
          if (colorSortMode == teamRedColorSort && readingBlue) {
            scoreFcn();
            intakeRollers.spin(reverse, 100, percent);
            upperRollers.spin(reverse, 100, percent);
            delay(500);
            sawBadColor = true;
          }

          if (colorSortMode == teamBlueColorSort && readingRed) {
            scoreFcn();
            intakeRollers.spin(reverse, 100, percent);
            upperRollers.spin(reverse, 100, percent);
            delay(500);
            sawBadColor = true;
          }
        }

        if (antiJam && not suspendLower) {
          if (upperRollers.velocity(pct) < 3) {
            upperRollers.spin(forward, 100, percent);
            upperRollers.spin(reverse, 100, percent);
            delay(100);
            upperRollers.spin(forward, 100, percent);
          }

          upperRollers.spin(forward, 100, percent);
          intakeRollers.spin(forward, 100, percent);
          delay(200);
        }

        break;

      case (midGoal):
        scoreFcn();

        if (autonSort) {
          if (colorSortMode == teamRedColorSort && readingBlue) {
            scoreFcn();
            intakeRollers.spin(reverse, 100, percent);
            upperRollers.spin(reverse, 100, percent);
            delay(500);
            sawBadColor = true;
          }

          if (colorSortMode == teamBlueColorSort && blockOtwIsRed) {
            scoreFcn();
            intakeRollers.spin(reverse, 100, percent);
            upperRollers.spin(reverse, 100, percent);
            delay(500);
            sawBadColor = true;
          }
        }

        break;

      case (midGoalSlow):
        upperRollers.spin(forward, slowMidSpeed, percent);
        intakeRollers.spin(forward, 100, percent);

        break;

      case (truncatedIntake):
        intakeRollers.spin(forward, 100, percent);
        upperRollers.stop(brake);

        break;

      case (topStageJamInsurance):
        intakeRollers.spin(forward, 100, percent);
        upperRollers.spin(reverse, 100, percent);
        delay(200);
        upperRollers.spin(forward, 100, percent);
        delay(200);

        break;

      case (antiSpill):
        intakeRollers.spin(forward, 100, percent);
        upperRollers.spin(reverse, 100, percent);

        break;

      case (onlyTopStage):
        intakeRollers.stop(coast);
        upperRollers.spin(forward, 50, percent);

        break;

      case (onlyBottomStage):
        intakeRollers.spin(forward, 100, percent);
        upperRollers.stop(brake);

        break;
    }

    if (justDropped4Bar) {
      upperRollers.spin(reverse, 100, percent);
      delay(200);
      justDropped4Bar = false;
    }

    vex::this_thread::sleep_for(10);
  }
  return 0;
}

// Mag monitoring task
int magMonitorFcn() {
  while (true) {
    if (OtwDistSensor.objectDistance(inches) < magDistanceThreshold) {
      blockOtw = true;
    } else {
      blockOtw = false;
    }

    if (OtwColorSensor.hue() < redThresholdUpper && OtwColorSensor.hue() > redThresholdLower) {
      readingRed = true;
      readingBlue = false;
    } else if (OtwColorSensor.hue() < blueThresholdUpper && OtwColorSensor.hue() > blueThresholdLower) {
      readingBlue = true;
      readingRed = false;
    } else {
      readingRed = false;
      readingBlue = false;
    }

    if (blockOtw && readingRed) {
      blockOtwIsRed = true;
      blockOtwIsBlue = false;
    } else if (blockOtw && readingBlue) {
      blockOtwIsBlue = true;
      blockOtwIsRed = false;
    } else {
      blockOtwIsRed = false;
      blockOtwIsBlue = false;
    }

    vex::this_thread::sleep_for(10);
  }
  return 0;
}

double raiseIntakeIn = 0;

int delayedIntakeLiftFcn() {
  while (true) {
    if (raiseIntakeIn != 0) {
      delay(raiseIntakeIn);
      midDescoreUp();
      raiseIntakeIn = 0;
    }
    vex::this_thread::sleep_for(10);
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

  OtwColorSensor.setLightPower(100);
  OtwColorSensor.setLight(ledState::on);
  OtwColorSensor.integrationTime(5);

  dropOdom();
  matchloaderUp();
  hookDown();
  retractMidGoalHook();

  colorSortMode = none;
  intakeMode = coasting;
  fourBarUp = true;

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
  autonSort = true;

  bool hookUnlocked;
  double trueH;

  if (didAuton) {
    hookUnlocked = true;
    matchloaderUp();
    liftOdom();
    intakeSort = true;
    antiJam = false;
  } else {
    hookUnlocked = false;
    antiJam = false;
  }

  liftOdom();
  intakeLag = false;
  retractMidGoalHook();
  liftOdom();

  //The actual LOOP is here. Everything above this will only be run ONCE
  while (true) {
    trueH = chassis.h;

    //BUTTON MONITOR////////////////////////////////////
    controllerButtonMonitoringFcn();
    ////////////////////////////////////////////////////

    //DRIVE CHOICE//////////////////////////////////////
    arcadeDriveFcn();
    ////////////////////////////////////////////////////

    //INTAKE////////////////////////////////////////////
    if (l2.Pressed_first) {
      midDescoreDown();
      hoodDown();
      raise4bar();
      intakeMode = intake;
    }
    /////////////////////////////////////////////////////

    //SPIT///////////////////////////////////////////////
    if (l1.Pressed_first) {
      midDescoreDown();
      intakeMode = spit;
    }

    if (l1.Pressed_first && l2.Pressed_second) {
      midDescoreUp();
      lowGoalSpeed = 50;
      intakeMode = lowGoal;
    }

    if (not l1.Pressed_first) {
      midDescoreDown();
    }
    /////////////////////////////////////////////////////

    //MID GOAL//////////////////////////////////////////
    if (r1.Pressed_first) {
      if (fourBarUp) {
        drop4bar();
        r1.Exhausted = true;
        justDropped4Bar = true;
      } else if (not r1.Exhausted) {
        intakeMode = midGoal;
        justDropped4Bar = false;
      }
    }

    if (r1.JustReleased) {
      r1.Exhausted = false;
    }
    /////////////////////////////////////////////////////

    //LONG GOAL//////////////////////////////////////////
    if (r2.Pressed_first) {
      if (not fourBarUp) {
        raise4bar();
        r2.Exhausted = true;
      } else if (not r2.Exhausted) {
        hoodUp();
        matchloaderUp();
        intakeMode = longGoal;
      }
    }

    if (r2.JustReleased) {
      r2.Exhausted = false;
    }

    if (a.NewPress) {
      hoodDown();
    }
    /////////////////////////////////////////////////////

    //COASTING///////////////////////////////////////////
    if (not l2.Pressed_first && not r2.Pressed_first && not r1.Pressed_first && not l1.Pressed_first) {
      intakeMode = coasting;
      firstLongGoal = false;
    }
    /////////////////////////////////////////////////////

    //HOOK///////////////////////////////////////////////
    if (Controller.ButtonDown.pressing()) {
      hookDown();
      hookUnlocked = true;
    } else {
      if (hookUnlocked) {
        hookUp();
      }
    }
    /////////////////////////////////////////////////////

    //MID GOAL HOOK /////////////////////////////////////
    if (rightArrow.NewPress || y.NewPress) {
      if (midGoalHookIsOut) {
        retractMidGoalHook();
      } else {
        extendMidGoalHook();
      }
    }
    /////////////////////////////////////////////////////

    //COLOR SORT CONTROL//////////////////////////////////
    if (leftArrow.NewPress) {
      if (colorSortMode == none) {
        // drawRedLogo();
        colorSortMode = teamRedColorSort;
      } else if (colorSortMode == teamRedColorSort) {
        // drawBlueLogo();
        colorSortMode = teamBlueColorSort;
      } else if (colorSortMode == teamBlueColorSort) {
        // drawNeutralLogo();
        colorSortMode = none;
      }

      if (didAuton) {
        colorSortMode = none;
      }

      Controller.rumble(".");
    }
    /////////////////////////////////////////////////////

    //MATCHLOADER CONTROL////////////////////////////////
    if (b.NewPress) {
      if (matchloaderIsDown) {
        matchloaderUp();
      } else {
        matchloaderDown();
      }
    }
    /////////////////////////////////////////////////////

    if (upArrow.NewPress) {
      driveTunerAuton(1,0);
    }

    wait(10, msec);
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
