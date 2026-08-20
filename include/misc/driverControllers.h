#pragma once

#include "vex.h"

using namespace vex;

// Read the controller sticks and write voltages to leftdrive/rightdrive.
void arcadeDriveFcn();
void singleStickDriveFcn();