// config.cpp — the numbers, and the velocity controllers they drive.

#include "ramsete/config.h"
#include "movement/odometry.h"
#include "misc/util.h"

Ramsete ramsete;

VelocityController velocityController;
VelocityController omegaController;

void setRamseteSettings() {
    //chassis specs
    ramsete.trackwidth = 12.0;    // in, wheel to wheel
    ramsete.maxVel     = 75.0;    // in/s, free speed on a straight
    ramsete.maxAccel   = 120.0;    // in/s²
    ramsete.sampleRate = 2.0;    // ms between baked rows; match the control loop

    //ramsete law
    // ramsete.b          = 0.02;    // stiffness, 1/in² (2654E's 45 /m², carried into inches)
    // ramsete.zeta       = 0.4;     // damping
    // ramsete.vMinGain   = 4.0;     // in/s floor inside k
    ramsete.b          = 0.02;    // stiffness, 1/in² (2654E's 45 /m², carried into inches)
    ramsete.zeta       = 0.4;     // damping
    ramsete.vMinGain   = 4.0;     // in/s floor inside k

    //velocity measurement smoothing -- chassis.v / chassis.ω, read back by the velocity layer
    ramsete.smoothV          = false;
    ramsete.smoothedVSamples = 5;
    ramsete.smoothω          = false;
    ramsete.smoothedωSamples = 2;

    //adaptive clock strength
    ramsete.clockCoupling = 8;

    //output slew -- caps the commanded v/ω after the law, 0 = uncapped
    ramsete.vMaxAccelRate = 0;      // in/s²
    ramsete.vMaxDecelRate = 0;      // in/s²
    ramsete.ωMaxAccelRate = 0;      // deg/s²
    ramsete.ωMaxDecelRate = 0;      // deg/s²

    //for moveTo
    ramsete.kLat         = 0.15;   // 1/in
}

void initializeVelocityControllerConstants() {
    velocityController.setSource(&chassis.v);
    // velocityController.setConstants(0.1226, 0.0193, 0.4833, 0.2739, 1.914);
    velocityController.setConstants(0.1226, 0.0193, 0.4833, 0, 0);
    velocityController.setIntegralSettings(0.5);

    omegaController.setSource(&chassis.ω, TO_DEG(1));   // chassis.ω is rad/s
    // omegaController.setConstants(0.0150, 0.00324, 1.105, 0.0389, 0.2541);
    omegaController.setConstants(0.0150, 0.00324, 1.105, 0, 0);
    omegaController.setIntegralSettings(0.5);
}