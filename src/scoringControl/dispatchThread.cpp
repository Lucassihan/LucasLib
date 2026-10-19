#include "scoringControl/dispatchThread.h"
#include "scoringControl/controlThread.h"
#include "misc/controllerButtonMonitoring.h"

double cFlat = 0;
double cTuck = claw.clawMin;
double cScore = 60;
double cOpen = 35;

double lPrime = 12;
double lLow = 0;
double lDrop = 4;
double lTuck = 5.5;
double lOpen = 0;

double step = 6.5/2.0;
double aGoalHeight = 0;
double nGoalHeight = 0;

bool overrideClawRollerControl;
bool finished;

enum RobotState clawTargetState;
enum RobotState clawCurrentState;
bool loadAutoClamp = false;

double h(double level, bool alliance) {
    return step*level + ((alliance) ? aGoalHeight : nGoalHeight);
}

void waitUntilFinished() {
    while (not finished) delay(10);
}

void clawAction(enum RobotState state, bool wait) {
    finished = false;
    clawTargetState = state;
    clawCurrentState = state;
    if (wait) waitUntilFinished();
}

void leaveChamber(double height) {
    finished = false;
    overrideClawRollerControl = true;
    claw.intake(12);
    lift.move(height);
    // if (r1.Pressing) {
    //     finished = true;
    //     overrideClawRollerControl = false;
    //     return;
    // }
    claw.move(cFlat);
    clawPID.maxOutput = 12;
    lift.waitUntil(5);
    lift.move(lTuck);
    claw.waitUntil(5);
    clawTargetState = Idle;
    finished = true;
    overrideClawRollerControl = false;
}

void dropClaw() {
    finished = false;
    claw.move(cFlat);
    lift.move(0);
    delay(200);
    lift.move(lPrime);

    lift.waitUntil(1);
    clawTargetState = Idle;
    finished = true;
}

void tuckClaw() {
    finished = false;

    if (lift.currentHeight < lTuck) {
        lift.move(lTuck+8);
        lift.waitUntil(5);
        lift.move(lTuck);
        claw.move(cTuck);
        lift.waitUntil(4);
    } else {
        claw.move(cTuck);
        lift.move(lTuck);
        lift.waitUntil(4);
    }
    
    clawTargetState = Idle;
    finished = true;
}

void score() {
    finished = false;
    overrideClawRollerControl = true;
    claw.move(cFlat);
    claw.spit();
    delay(200);
    lift.move(lift.currentHeight + 6);
    lift.waitUntil(3);
    claw.move(cScore);

    claw.waitUntil(5);
    clawTargetState = Idle;
    finished = true;
    overrideClawRollerControl = false;
}

void open() {
    finished = false;
    // lift.move(2);
    claw.move(cOpen);
    claw.waitUntil(20);
    lift.move(2);
    lift.waitUntil(5);
    delay(200);

    clawTargetState = Idle;
    finished = true;
}

int clawControlTask() {
    while (true) {
        claw.update();
        lift.update();
        delay(10);
    }
}

int clawDispatchTask() {
    while (true) {
        switch (clawTargetState) {
            case Idle: 
                break;
            case LeaveChamber: 
                leaveChamber();
                break;
            case Drop:
                dropClaw();
                break;
            case Tuck:
                tuckClaw();
                break;
            case Score:
                score();    
                break;
            case Open:
                open();
        }
        delay(10);
    }
}
