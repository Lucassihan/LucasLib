#pragma once

#include "vex.h"
#include <string>

using namespace vex;

// ---- Auton tracking ----

extern std::string currentAuton;
extern bool didAuton;
extern int  autonIndex;

// ---- Mechanism motors ----

extern motor intakeRollers;
extern motor upperRollers;
extern motor clawMotor;
extern motor_group liftMotor;
extern motor clawRollers;
extern motor intakeMotor;

// ---- Mechanism sensors ----

extern distance OtwDistSensor;
extern distance ChamberDistSensor;
extern optical  OtwColorSensor;
extern distance autoClampSensor;

// ---- Pneumatics ----

extern digital_out hood;
extern digital_out hook;
extern digital_out matchloader;
extern digital_out midDescore;
extern digital_out intakeLift;
extern digital_out odomLift;
extern digital_out midGoalHook;

// ---- Timers ----

extern timer R2Time;

// ---- Pneumatic state + helpers ----

extern bool midDescoreIsUp;
extern bool matchloaderIsDown;
extern bool fourBarUp;
extern bool midGoalHookIsOut;

void midDescoreUp();
void midDescoreDown();
void midDescoreToggle();
void hookUp();
void hookDown();
void hoodUp();
void hoodDown();
void matchloaderUp();
void matchloaderDown();
void liftOdom();
void dropOdom();
void raise4bar();
void drop4bar();
void extendMidGoalHook();
void retractMidGoalHook();

// ---- Roller helpers ----

extern double lowGoalSpeed;

void coastAllRollers();
void intakeFcn();
void scoreFcn();
void spitFcn();
void lowGoalFcn(bool suspendUpper = false);

// ---- Intake + color-sort runtime state ----

extern bool blockOtw;
extern bool readingRed;
extern bool readingBlue;
extern bool blockOtwIsRed;
extern bool blockOtwIsBlue;

extern double redThresholdUpper;
extern double redThresholdLower;
extern double blueThresholdUpper;
extern double blueThresholdLower;

extern double magDistanceThreshold;
extern double chamberDistanceThreshold;

extern double reverseTimeLongSort;
extern double reverseTimeMidSort;

extern double chamberPurgeTime;
extern bool   purgingDuringStore;
extern int    midGoalSpeed;
extern double lowGoalIn;
extern bool   intakeAntiJamEnabled;
extern double startPrimingAfter;
extern bool   startPrimingLowGoal;
extern double spitIn;
extern bool   suspendUpperRollers;
extern bool   slowLongGoal;
extern bool   wasJustIntaking;
extern double score3Time;
extern bool   firstLongGoal;
extern bool   intakeLag;
extern bool   autonSort;
extern bool   intakeSort;
extern bool   sawBadColor;
extern double slowMidSpeed;
extern bool   antiJam;
extern bool   justDropped4Bar;
extern bool   suspendLower;
extern double raiseIntakeIn;
extern bool   printingDistances;

// ---- Background tasks ----

int magControlFcn();
int magMonitorFcn();
int delayedIntakeLiftFcn();
int trackDisplayTask();
int controllerScreenTask();
int brainScreenTaskFcn();
int fourBarControlTask();
int autoClampTask();
int scoopTask();

// ---- Lifecycle ----

extern competition Competition;

void initialize();
void pre_auton();
void autonomous();
void driver();