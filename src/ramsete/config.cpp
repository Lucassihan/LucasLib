// config.cpp — the numbers, and the velocity controllers they drive.

#include "ramsete/config.h"
#include "movement/odometry.h"
#include "misc/util.h"

Ramsete ramsete;

VelocityController velocityController;
VelocityController omegaController;

void setRamseteSettings() {
    //chassis specs
    ramsete.trackwidth = 12.5;    // in, wheel to wheel
    ramsete.maxVel     = 90.0;    // in/s, free speed on a straight
    ramsete.maxAccel   = 120.0;    // in/s²
    ramsete.sampleRate = 10.0;    // ms between baked rows; match the control loop

    //ramsete law
    ramsete.b          = 0.03;    // stiffness, 1/in² (2654E's 45 /m², carried into inches)
    ramsete.zeta       = 0.4;     // damping
    ramsete.vMinGain   = 4.0;     // in/s floor inside k

    //adaptive clock strength
    ramsete.clockCoupling = 6.0;

    //for moveTo
    ramsete.kLat         = 0.02;   // 1/in
}

void initializeVelocityControllerConstants() {
    velocityController.setSource(&chassis.v);
    velocityController.setConstants(0.1224, 0.0182, 0.5167, 0.2535, 1.733);
    velocityController.setIntegralSettings(0);

    omegaController.setSource(&chassis.ω, TO_DEG(1));   // chassis.ω is rad/s
    omegaController.setConstants(0.0145, 0.00361, 1.129, 0.0327, 0.1727);
    omegaController.setIntegralSettings(0);
}