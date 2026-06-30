#pragma once

#include "vex.h"

using namespace vex;

// Classic 2-stick drive: one axis for forward/back, one for turn.
// Reads the controller and writes voltages to leftdrive/rightdrive.
void arcadeDriveFcn();
void singleStickDriveFcn();