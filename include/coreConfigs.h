#pragma once

#include "vex.h"
#include "movement/odometry.h"

using namespace vex;

// =========================================================================
// Brain + controller
// =========================================================================

extern brain      Brain;
extern controller Controller;

// =========================================================================
// Chassis motors
// =========================================================================

extern motor fl;
extern motor ml;
extern motor bl;

extern motor fr;
extern motor mr;
extern motor br;

extern motor_group leftdrive;
extern motor_group rightdrive;

// =========================================================================
// Heading + tracking sensors
// =========================================================================

extern inertial IMU1;
extern inertial IMU2;

extern rotation TX;
extern rotation TY;

// =========================================================================
// Wall-reading distance sensors
// =========================================================================

extern distance DistSensor1;
extern distance DistSensor2;
extern distance DistSensor3;
extern distance DistSensor4;
