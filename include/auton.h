#pragma once

#include "tuner.h"
#include "ramsete/ramsete.h"

// Baked at startup by createTrajectories(), driven by the routes below.
extern Trajectory goTraj;
extern Trajectory loadTraj;
void createTrajectories();

void exampleAuto();
void redAuto();
void blueAuto();

void trajectoryTest();
