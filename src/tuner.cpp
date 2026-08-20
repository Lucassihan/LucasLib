#include "tuner.h"
#include "movement.h"
#include "coreConfigs.h"
#include "misc/util.h"

#include <cmath>

extern bool printingDistances;

bool stopTuning = false;
static bool g_tuneAborted = false;

static bool tunerKilled() {
    if (Controller.ButtonB.pressing()) {
        stopTuning = true;
    }
    return stopTuning;
}

// =========================================================================
//   Turn PID auto-tuner
// =========================================================================

struct TurnTuneCfg {
    double kP = 0;
    double kI = 0;

    double testAngle = 90;

    double kDStart = 0;
    double kDMin = 0;
    double kDMax = 0;
    double growth = 1.4;
    int    refineIters = 6;

    double testDuration = 2500;
    double settleTol = 0.75;
    double settleHold = 150;
    double ssWindow = 300;

    double overshootTol = 1.5;
    int    oscTol = 1;

    double wOvershoot = 2.0;
    double wOscillation = 3.0;
    double wRise = 2.0;
    double wSettle = 1.0;
    double wSteady = 4.0;

    double settleBetween = 400;
};

struct TurnTestResult {
    double overshoot;
    int    oscillations;
    double riseTime;
    double settleTime;
    double steadyError;
    double cost;
};

static TurnTestResult runTurnTest(const TurnTuneCfg& cfg, double kD, double targetH) {
    TurnTestResult r;
    r.overshoot = 0;
    r.oscillations = 0;
    r.riseTime = cfg.testDuration;
    r.settleTime = cfg.testDuration;
    r.steadyError = 0;
    r.cost = 0;

    turnPID.clearAllValues();
    turnPID.tempConstants(cfg.kP, cfg.kI, kD);

    double startTime = Brain.timer(msec);
    double elapsed = 0;

    int    initialSign = 0;
    int    lastSign = 0;
    bool   reached = false;
    bool   settled = false;
    double settleEnterTime = -1;

    double ssSum = 0;
    int    ssCount = 0;

    while (elapsed < cfg.testDuration) {
        if (tunerKilled()) {
            g_tuneAborted = true;
            break;
        }

        double error = chassis.wrappedHeadingError(targetH);
        double turnPow = turnPID.calculateOutput(error);
        voltageDrive(turnPow, -turnPow);

        int s = sign(error);

        if (initialSign == 0 && s != 0) {
            initialSign = s;
            lastSign = s;
        }

        if (s != 0 && lastSign != 0 && s != lastSign) {
            r.oscillations++;
        }
        if (s != 0) {
            lastSign = s;
        }

        if (initialSign != 0 && s == -initialSign) {
            double mag = fabs(error);
            if (mag > r.overshoot) {
                r.overshoot = mag;
            }
        }

        if (not reached && fabs(error) <= cfg.settleTol) {
            reached = true;
            r.riseTime = elapsed;
        }

        if (fabs(error) <= cfg.settleTol) {
            if (settleEnterTime < 0) {
                settleEnterTime = elapsed;
            }
            if (not settled && (elapsed - settleEnterTime) >= cfg.settleHold) {
                settled = true;
                r.settleTime = settleEnterTime;
            }
        } else {
            settleEnterTime = -1;
        }

        if (elapsed >= cfg.testDuration - cfg.ssWindow) {
            ssSum += fabs(error);
            ssCount++;
        }

        delay(10);
        elapsed = Brain.timer(msec) - startTime;
    }

    voltageDrive(0, 0);

    if (ssCount > 0) {
        r.steadyError = ssSum / ssCount;
    }

    r.cost = cfg.wOvershoot * r.overshoot
           + cfg.wOscillation * (double)r.oscillations
           + cfg.wRise * (r.riseTime / 1000.0)
           + cfg.wSettle * (r.settleTime / 1000.0)
           + cfg.wSteady * r.steadyError;

    return r;
}

static void printTurnTuneStatus(const char* phase, double kP, double kD,
                                const TurnTestResult& r, double bestKD, double bestCost) {
    Brain.Screen.printAt(5, 40,  true, "Turn PID Tuner  [%s]        ", phase);
    Brain.Screen.printAt(5, 70,  true, "kP %.3f   testing kD %.3f        ", kP, kD);
    Brain.Screen.printAt(5, 100, true, "overshoot    %.2f deg        ", r.overshoot);
    Brain.Screen.printAt(5, 130, true, "oscillations %d              ", r.oscillations);
    Brain.Screen.printAt(5, 160, true, "rise %.0f  settle %.0f ms        ", r.riseTime, r.settleTime);
    Brain.Screen.printAt(5, 190, true, "steady err   %.2f deg        ", r.steadyError);
    Brain.Screen.printAt(5, 220, true, "cost %.2f   best kD %.3f (%.2f)     ",
                         r.cost, bestKD, bestCost);
}

static TurnTestResult runTurnTestAt(const TurnTuneCfg& cfg, double kD, const char* phase,
                                    double& dir, double& bestKD, double& bestCost) {
    double target = chassis.wrapAngle(chassis.h + dir * cfg.testAngle);
    dir = -dir;

    TurnTestResult r = runTurnTest(cfg, kD, target);

    if (r.cost < bestCost) {
        bestCost = r.cost;
        bestKD = kD;
    }

    printTurnTuneStatus(phase, cfg.kP, kD, r, bestKD, bestCost);
    if (not g_tuneAborted) {
        delay(cfg.settleBetween);
    }
    return r;
}

void turnTunerAuton(double kP, double startKD) {
    printingDistances = false;
    g_tuneAborted = false;
    stopTuning = false;
    Brain.Screen.setFont(mono20);
    Brain.Screen.clearScreen();

    TurnTuneCfg cfg;
    cfg.kP = kP;
    cfg.kDStart = startKD;

    double kDStart = cfg.kDStart;
    if (kDStart <= 0) {
        kDStart = cfg.kP * 2.0;
    }
    if (kDStart <= 0) {
        kDStart = 1.0;
    }

    double kDMax = cfg.kDMax;
    if (kDMax <= 0) {
        kDMax = cfg.kP * 200.0;
    }
    if (kDMax <= 0) {
        kDMax = 200.0;
    }

    double kDMin = cfg.kDMin;
    if (kDMin <= 0) {
        kDMin = cfg.kP * 0.5;
    }
    if (kDMin <= 0) {
        kDMin = 0.1;
    }

    double dir = 1.0;
    double bestKD = kDStart;
    double bestCost = 1e18;

    double kDLow = kDMin;
    double kDHigh = kDMax;

    TurnTestResult r0 = runTurnTestAt(cfg, kDStart, "BRACKET", dir, bestKD, bestCost);
    bool startStable = (r0.overshoot <= cfg.overshootTol) && (r0.oscillations <= cfg.oscTol);

    if (startStable) {
        kDHigh = kDStart;
        double kD = kDStart / cfg.growth;
        while (kD >= kDMin) {
            if (g_tuneAborted) {
                break;
            }
            TurnTestResult r = runTurnTestAt(cfg, kD, "DESCEND", dir, bestKD, bestCost);
            bool stable = (r.overshoot <= cfg.overshootTol) && (r.oscillations <= cfg.oscTol);
            if (not stable) {
                kDLow = kD;
                break;
            }
            kDHigh = kD;
            kD = kD / cfg.growth;
        }
    } else {
        kDLow = kDStart;
        bool found = false;
        double kD = kDStart * cfg.growth;
        while (kD <= kDMax) {
            if (g_tuneAborted) {
                break;
            }
            TurnTestResult r = runTurnTestAt(cfg, kD, "ASCEND", dir, bestKD, bestCost);
            bool stable = (r.overshoot <= cfg.overshootTol) && (r.oscillations <= cfg.oscTol);
            if (stable) {
                kDHigh = kD;
                found = true;
                break;
            }
            kDLow = kD;
            kD = kD * cfg.growth;
        }
        if (not found) {
            kDHigh = kDMax;
        }
    }

    double a = kDLow;
    double b = kDHigh;
    const double invphi = 0.6180339887;

    double c = b - invphi * (b - a);
    double d = a + invphi * (b - a);

    double fc = 0;
    double fd = 0;
    if (not g_tuneAborted) {
        fc = runTurnTestAt(cfg, c, "REFINE", dir, bestKD, bestCost).cost;
    }
    if (not g_tuneAborted) {
        fd = runTurnTestAt(cfg, d, "REFINE", dir, bestKD, bestCost).cost;
    }

    for (int i = 0; i < cfg.refineIters; i++) {
        if (g_tuneAborted) {
            break;
        }
        if (fc < fd) {
            b = d;
            d = c;
            fd = fc;
            c = b - invphi * (b - a);
            fc = runTurnTestAt(cfg, c, "REFINE", dir, bestKD, bestCost).cost;
        } else {
            a = c;
            c = d;
            fc = fd;
            d = a + invphi * (b - a);
            fd = runTurnTestAt(cfg, d, "REFINE", dir, bestKD, bestCost).cost;
        }
    }

    voltageDrive(0, 0);

    Brain.Screen.clearScreen();
    if (g_tuneAborted) {
        Brain.Screen.printAt(5, 70, true, "TUNING ABORTED - best so far");
    } else {
        Brain.Screen.printAt(5, 70, true, "TUNING COMPLETE");
    }
    Brain.Screen.printAt(5, 110, true, "kP    %.3f", cfg.kP);
    Brain.Screen.printAt(5, 150, true, "kD    %.3f   <-- use this", bestKD);
    Brain.Screen.printAt(5, 190, true, "cost  %.2f", bestCost);

    Controller.Screen.clearScreen();
    Controller.Screen.setCursor(1, 1);
    Controller.Screen.print("kP %.3f", cfg.kP);
    Controller.Screen.setCursor(2, 1);
    Controller.Screen.print("best kD %.3f", bestKD);
    Controller.rumble("---");
}

// =========================================================================
//   Drive PID auto-tuner
// =========================================================================
//
// Same bracket → golden-section approach as the turn tuner, but drives the
// robot forward/backward in a straight line. headingPID holds the current
// heading so the robot doesn't veer. The robot alternates direction each
// test so it stays in roughly the same area.
//
// Needs ~4+ feet of clear floor in front of the robot.

struct DriveTuneCfg {
    double kP = 0;
    double kI = 0;

    double testDistance = 24;   // inches per test drive

    double kDStart = 0;
    double kDMin = 0;
    double kDMax = 0;
    double growth = 1.4;
    int    refineIters = 6;

    double testDuration = 3000;
    double settleTol = 0.5;    // inches
    double settleHold = 150;
    double ssWindow = 300;

    double overshootTol = 0.5; // inches
    int    oscTol = 0;

    double wOvershoot = 2.0;
    double wOscillation = 3.0;
    double wRise = 2.0;
    double wSettle = 1.0;
    double wSteady = 4.0;

    double settleBetween = 400;
};

struct DriveTestResult {
    double overshoot;
    int    oscillations;
    double riseTime;
    double settleTime;
    double steadyError;
    double cost;
};

static DriveTestResult runDriveTest(const DriveTuneCfg& cfg, double kD, double distance) {
    DriveTestResult r;
    r.overshoot = 0;
    r.oscillations = 0;
    r.riseTime = cfg.testDuration;
    r.settleTime = cfg.testDuration;
    r.steadyError = 0;
    r.cost = 0;

    drivePID.clearAllValues();
    drivePID.tempConstants(cfg.kP, cfg.kI, kD);
    drivePID.maxAccelRate = 0;
    drivePID.maxDecelRate = 0;
    headingPID.clearAllValues();

    double holdHeading = chassis.h;
    chassis.clearTrackedInches();

    double startTime = Brain.timer(msec);
    double elapsed = 0;

    int    initialSign = 0;
    int    lastSign = 0;
    bool   reached = false;
    bool   settled = false;
    double settleEnterTime = -1;

    double ssSum = 0;
    int    ssCount = 0;

    while (elapsed < cfg.testDuration) {
        if (tunerKilled()) {
            g_tuneAborted = true;
            break;
        }

        double error = distance - chassis.yInchesTracked;
        double driveOutput = drivePID.calculateOutput(error);

        double hError = chassis.wrappedHeadingError(holdHeading);
        double headingOutput = headingPID.calculateOutput(hError);

        double left = driveOutput + headingOutput;
        double right = driveOutput - headingOutput;
        voltageDrive(left, right);

        int s = sign(error);

        if (initialSign == 0 && s != 0) {
            initialSign = s;
            lastSign = s;
        }

        if (s != 0 && lastSign != 0 && s != lastSign) {
            r.oscillations++;
        }
        if (s != 0) {
            lastSign = s;
        }

        if (initialSign != 0 && s == -initialSign) {
            double mag = fabs(error);
            if (mag > r.overshoot) {
                r.overshoot = mag;
            }
        }

        if (not reached && fabs(error) <= cfg.settleTol) {
            reached = true;
            r.riseTime = elapsed;
        }

        if (fabs(error) <= cfg.settleTol) {
            if (settleEnterTime < 0) {
                settleEnterTime = elapsed;
            }
            if (not settled && (elapsed - settleEnterTime) >= cfg.settleHold) {
                settled = true;
                r.settleTime = settleEnterTime;
            }
        } else {
            settleEnterTime = -1;
        }

        if (elapsed >= cfg.testDuration - cfg.ssWindow) {
            ssSum += fabs(error);
            ssCount++;
        }

        delay(10);
        elapsed = Brain.timer(msec) - startTime;
    }

    voltageDrive(0, 0);

    if (ssCount > 0) {
        r.steadyError = ssSum / ssCount;
    }

    r.cost = cfg.wOvershoot * r.overshoot
           + cfg.wOscillation * (double)r.oscillations
           + cfg.wRise * (r.riseTime / 1000.0)
           + cfg.wSettle * (r.settleTime / 1000.0)
           + cfg.wSteady * r.steadyError;

    return r;
}

static void printDriveTuneStatus(const char* phase, double kP, double kD,
                                 const DriveTestResult& r, double bestKD, double bestCost) {
    Brain.Screen.printAt(5, 40,  true, "Drive PID Tuner  [%s]        ", phase);
    Brain.Screen.printAt(5, 70,  true, "kP %.3f   testing kD %.3f        ", kP, kD);
    Brain.Screen.printAt(5, 100, true, "overshoot    %.2f in        ", r.overshoot);
    Brain.Screen.printAt(5, 130, true, "oscillations %d              ", r.oscillations);
    Brain.Screen.printAt(5, 160, true, "rise %.0f  settle %.0f ms        ", r.riseTime, r.settleTime);
    Brain.Screen.printAt(5, 190, true, "steady err   %.2f in        ", r.steadyError);
    Brain.Screen.printAt(5, 220, true, "cost %.2f   best kD %.3f (%.2f)     ",
                         r.cost, bestKD, bestCost);
}

static DriveTestResult runDriveTestAt(const DriveTuneCfg& cfg, double kD, const char* phase,
                                      double& dir, double& bestKD, double& bestCost) {
    double distance = dir * cfg.testDistance;
    dir = -dir;

    DriveTestResult r = runDriveTest(cfg, kD, distance);

    if (r.cost < bestCost) {
        bestCost = r.cost;
        bestKD = kD;
    }

    printDriveTuneStatus(phase, cfg.kP, kD, r, bestKD, bestCost);
    if (not g_tuneAborted) {
        delay(cfg.settleBetween);
    }
    return r;
}

void driveTunerAuton(double kP, double startKD) {
    printingDistances = false;
    g_tuneAborted = false;
    stopTuning = false;
    Brain.Screen.setFont(mono20);
    Brain.Screen.clearScreen();

    DriveTuneCfg cfg;
    cfg.kP = kP;
    cfg.kDStart = startKD;

    double kDStart = cfg.kDStart;
    if (kDStart <= 0) {
        kDStart = cfg.kP * 2.0;
    }
    if (kDStart <= 0) {
        kDStart = 1.0;
    }

    double kDMax = cfg.kDMax;
    if (kDMax <= 0) {
        kDMax = cfg.kP * 200.0;
    }
    if (kDMax <= 0) {
        kDMax = 200.0;
    }

    double kDMin = cfg.kDMin;
    if (kDMin <= 0) {
        kDMin = cfg.kP * 0.5;
    }
    if (kDMin <= 0) {
        kDMin = 0.1;
    }

    double dir = 1.0;
    double bestKD = kDStart;
    double bestCost = 1e18;

    double kDLow = kDMin;
    double kDHigh = kDMax;

    DriveTestResult r0 = runDriveTestAt(cfg, kDStart, "BRACKET", dir, bestKD, bestCost);
    bool startStable = (r0.overshoot <= cfg.overshootTol) && (r0.oscillations <= cfg.oscTol);

    if (startStable) {
        kDHigh = kDStart;
        double kD = kDStart / cfg.growth;
        while (kD >= kDMin) {
            if (g_tuneAborted) {
                break;
            }
            DriveTestResult r = runDriveTestAt(cfg, kD, "DESCEND", dir, bestKD, bestCost);
            bool stable = (r.overshoot <= cfg.overshootTol) && (r.oscillations <= cfg.oscTol);
            if (not stable) {
                kDLow = kD;
                break;
            }
            kDHigh = kD;
            kD = kD / cfg.growth;
        }
    } else {
        kDLow = kDStart;
        bool found = false;
        double kD = kDStart * cfg.growth;
        while (kD <= kDMax) {
            if (g_tuneAborted) {
                break;
            }
            DriveTestResult r = runDriveTestAt(cfg, kD, "ASCEND", dir, bestKD, bestCost);
            bool stable = (r.overshoot <= cfg.overshootTol) && (r.oscillations <= cfg.oscTol);
            if (stable) {
                kDHigh = kD;
                found = true;
                break;
            }
            kDLow = kD;
            kD = kD * cfg.growth;
        }
        if (not found) {
            kDHigh = kDMax;
        }
    }

    double a = kDLow;
    double b = kDHigh;
    const double invphi = 0.6180339887;

    double c = b - invphi * (b - a);
    double d = a + invphi * (b - a);

    double fc = 0;
    double fd = 0;
    if (not g_tuneAborted) {
        fc = runDriveTestAt(cfg, c, "REFINE", dir, bestKD, bestCost).cost;
    }
    if (not g_tuneAborted) {
        fd = runDriveTestAt(cfg, d, "REFINE", dir, bestKD, bestCost).cost;
    }

    for (int i = 0; i < cfg.refineIters; i++) {
        if (g_tuneAborted) {
            break;
        }
        if (fc < fd) {
            b = d;
            d = c;
            fd = fc;
            c = b - invphi * (b - a);
            fc = runDriveTestAt(cfg, c, "REFINE", dir, bestKD, bestCost).cost;
        } else {
            a = c;
            c = d;
            fc = fd;
            d = a + invphi * (b - a);
            fd = runDriveTestAt(cfg, d, "REFINE", dir, bestKD, bestCost).cost;
        }
    }

    voltageDrive(0, 0);

    Brain.Screen.clearScreen();
    if (g_tuneAborted) {
        Brain.Screen.printAt(5, 70, true, "TUNING ABORTED - best so far");
    } else {
        Brain.Screen.printAt(5, 70, true, "TUNING COMPLETE");
    }
    Brain.Screen.printAt(5, 110, true, "kP    %.3f", cfg.kP);
    Brain.Screen.printAt(5, 150, true, "kD    %.3f   <-- use this", bestKD);
    Brain.Screen.printAt(5, 190, true, "cost  %.2f", bestCost);

    Controller.Screen.clearScreen();
    Controller.Screen.setCursor(1, 1);
    Controller.Screen.print("kP %.3f", cfg.kP);
    Controller.Screen.setCursor(2, 1);
    Controller.Screen.print("best kD %.3f", bestKD);
    Controller.rumble("---");
}
