#pragma once

#include "main.h"
#include "exitConditions.h"
#include "odometry.h"
#include "pid.h"
#include "controlThread.h"

Claw claw;
Lift lift;

//claw presets
double cFlat = 0;
double cTuck = -90;
double cOpen = 45;

//lift presets
double lOpen = 4;
double lPrime = 10;
double lLow = 0;
double lDrop = 4;
double lTuck = 4;

void leaveChamber() {
    lift.move(lPrime);
    claw.move(cOpen);
    clawPID.maxOutput = 4;
    lift.waitUntil(3);
    clawPID.maxOutput = 12;
}

void dropClaw() {
    claw.move(cFlat);
    lift.move(lDrop);
}

void tuckClaw() {
    lift.move(lift.currentHeight + 12);
    lift.waitUntil(4);    

    claw.move(cTuck);
    claw.waitUntil(30);

    lift.move(lTuck);
}

void score() {
    claw.move(cFlat);
    lift.move(lift.currentHeight + 1);
    claw.waitUntil(2);
    claw.spit();
    delay(200);
    lift.move(lift.currentHeight + 5);
    delay(100);
    claw.move(cFlat);
    claw.stop();
}

enum ClawState {
    LeaveChamber,
    Drop,
    Tuck,
    Score
};

ClawState clawState;

int clawControlTask() {
    while (true) {
        claw.update();
        lift.update();
        delay(10);
    }
}

int clawDispatchTask() {
    while (true) {
        switch (clawState) {
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
        }
    }
}