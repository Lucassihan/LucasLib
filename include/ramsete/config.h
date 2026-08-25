#pragma once

#include "ramsete/util/ramseteController.h"
#include "ramsete/velocityController.h"

// config — where the module's numbers get set. Both functions are called from autonSettings() the way initializePIDConstants() is; until they run the constants are zero and the drivetrain will not move.
// moveTo is the exception: it runs on drivePID and headingPID, so only kLat is set here.

// The velocity layer's two channels: forward speed and turn rate.
extern VelocityController velocityController;
extern VelocityController omegaController;

void setRamseteSettings();                      // the law and the chassis limits
void initializeVelocityControllerConstants();   // wires each channel to what it measures, and its gains
