#include "cmath"
#include "misc/driverControllers.h"
#include "coreConfigs.h"

void arcadeDriveFcn() {
    double turnImportance = 0.13; //0.2 on omnis, 0.13 on grips

    fl.setStopping(coast);
    ml.setStopping(coast);
    bl.setStopping(coast);
    fr.setStopping(coast);
    mr.setStopping(coast);
    br.setStopping(coast);

    double turnVal = Controller.Axis1.position(percent);
    double forwardVal = Controller.Axis3.position(percent);

    // Expo curves, 0: y=x, 1: y=x^3
    // Turn scaling, tune kTurn between 0 and 1
    double kTurn = 0;
    double turnNorm = turnVal / 100.0;
    turnVal = ((1 - kTurn) * turnNorm + kTurn * turnNorm * turnNorm * turnNorm) * 100.0;

    // Forward scaling
    double kForward = 0;
    double forwardNorm = forwardVal / 100.0;
    forwardVal = ((1 - kForward) * forwardNorm + kForward * forwardNorm * forwardNorm * forwardNorm) * 100.0;

    // Slew rate limiter — prevents tippy direction reversals on Axis3
    static double slewedForward = 0.0;
    double maxSlew = 6.0;
    double slewDelta = forwardVal - slewedForward;
    if (slewDelta >  maxSlew) {
        slewDelta =  maxSlew;
    }
    if (slewDelta < -maxSlew) {
        slewDelta = -maxSlew;
    }
    slewedForward += slewDelta;
    forwardVal = slewedForward;

    // Convert to volts
    double turnVolts    = turnVal * 0.12;
    double forwardVolts = forwardVal * 0.12 * (1 - (std::abs(turnVolts) / 120.0) * turnImportance);

    double leftVolts  = forwardVolts + turnVolts;
    double rightVolts = forwardVolts - turnVolts;

    fl.spin(forward, leftVolts,  voltageUnits::volt);
    ml.spin(forward, leftVolts,  voltageUnits::volt);
    bl.spin(forward, leftVolts,  voltageUnits::volt);
    fr.spin(forward, rightVolts, voltageUnits::volt);
    mr.spin(forward, rightVolts, voltageUnits::volt);
    br.spin(forward, rightVolts, voltageUnits::volt);
}