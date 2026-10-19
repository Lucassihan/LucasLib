#pragma once

#include "main.h"
#include "exitConditions.h"
#include "odometry.h"
#include "pid.h"
#include "controlThread.h"

//claw presets
extern double cFlat;
extern double cTuck;
extern double cScore;
extern double cOpen;

//lift presets
extern double lPrime;
extern double lLow;
extern double lDrop;
extern double lTuck;
extern double lOpen;

extern double step;
extern bool overrideClawRollerControl;
extern bool finished;
void waitUntilFinished();

enum RobotState {
    //General purpose
    Idle,

    //Intake
    Intake,
    Spit,

    //Claw
    LeaveChamber,
    Score,
    Open,
    Drop,
    Tuck
};

extern enum RobotState clawTargetState;
extern enum RobotState clawCurrentState;
extern bool loadAutoClamp;

//hands the state to clawDispatchTask. wait = true blocks until that action reports finished.
void clawAction(enum RobotState state, bool wait = false);

double h(double h, bool alliance = false);

void leaveChamber(double height = lPrime);
void dropClaw();
void tuckClaw();
void score();
void frontIntake();
void open();

int clawControlTask();
int clawDispatchTask();
