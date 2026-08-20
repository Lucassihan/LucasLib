// velocityTuning.cpp — one file of step responses; the app derives the sweep.

#include "ramsete/velocityTuning.h"
#include "coreConfigs.h"
#include "movement/odometry.h"
#include "misc/util.h"

#include <cmath>
#include <cstdio>
#include <cstring>

VelocityTuner velocityTuner;

namespace {
constexpr int    CYCLE_MS  = 10;
constexpr int    SCREEN_MS = 100;   // the controller screen is rate-limited; faster just floods the radio
constexpr double MAX_VOLTS = 12.0;

double now() {
    return vex::timer::system() / 1000.0;
}
}  // namespace

SweepChannel::SweepChannel(SweepAxis axis, double drift, const char* prefix)
    : drift(drift), axis(axis), prefix(prefix) {}

const char* SweepChannel::unit() const {
    return axis == SweepAxis::Linear ? "in/s" : "deg/s";
}

double SweepChannel::measure() const {
    // chassis.ω is rad/s; the turn controller is scaled to deg/s, so the data
    // it gets tuned from has to be too.
    return axis == SweepAxis::Linear ? chassis.v : TO_DEG(chassis.ω);
}

void SweepChannel::drive(double volts) const {
    if (axis == SweepAxis::Linear) voltageDrive(volts, volts);
    else                           voltageDrive(volts, -volts);  // CW+, matching heading
}

void SweepChannel::collectData(double startVolts, double stepSize, double timeout, double minDwell) {
    nextVolts = takeSmallerBetween(startVolts + (trial - 1) * stepSize, MAX_VOLTS);

    bool aNow    = Controller.ButtonA.pressing();
    bool upNow   = Controller.ButtonUp.pressing();
    bool downNow = Controller.ButtonDown.pressing();

    if (upNow && !upPrev) positive = true;
    if (downNow && !downPrev) positive = false;

    bool start = aNow && !aPrev;

    aPrev    = aNow;
    upPrev   = upNow;
    downPrev = downNow;

    if (start) runTrial(nextVolts, timeout, minDwell);
}

void SweepChannel::runTrial(double volts, double timeout, double minDwell) {
    const int w = window < 1 ? 1 : window;

    // The rise is buffered, not streamed: an fwrite inside the loop would stall
    // the sample cadence it is trying to record. It doubles as the settle
    // history, so there is no second ring to keep in step with it.
    std::vector<Vector2> rise;
    rise.reserve((int)(timeout * 1000 / CYCLE_MS) + 8);

    const double dir = positive ? 1.0 : -1.0;
    const double t0  = now();

    drive(dir * volts);

    bool   discard = false;   // B, or a trial that never settled
    double settled = 0;

    // Mean of the `w` samples ending `back` from the end.
    auto meanBack = [&rise, w](int back) {
        double sum = 0;
        for (int i = 0; i < w; i++) sum += rise[rise.size() - 1 - back - i].y;
        return sum / w;
    };

    while (true) {
        if (Controller.ButtonB.pressing()) {
            discard = true;
            break;
        }

        // Timestamp then sample, as close together as possible, and log the
        // real elapsed time rather than assuming the loop held its period.
        double elapsed = now() - t0;
        rise.push_back({elapsed, dir * measure()});

        // Settled = the window mean has stopped climbing. Comparing two means
        // cancels the noise that makes a spread test fire while the rise is
        // still going; see the note in the header.
        if ((int)rise.size() >= 2 * w && elapsed >= minDwell) {
            double recent = meanBack(0);
            if (fabs(recent - meanBack(w)) <= drift) {
                settled = recent;
                break;
            }
        }

        // A trial that never settles is not a measurement, so it is thrown
        // away exactly like a B abort — same voltage reruns on the next A.
        if (elapsed >= timeout) {
            discard = true;
            break;
        }

        delay(CYCLE_MS);
    }

    leftdrive.stop(brake);
    rightdrive.stop(brake);

    if (discard) {
        // Nothing recorded and trial is untouched, so the next A press reruns
        // this same voltage. Wait out the release so it can't leak anywhere.
        while (Controller.ButtonB.pressing()) delay(CYCLE_MS);
        return;
    }

    // Both directions measure the same drive, so a negative run folds back into
    // quadrant one — the fits model volts = kV·v + kS·sign(v) and a rise toward
    // a positive asymptote. The rise was folded by dir as it was sampled, so
    // the settled mean is already positive.
    lastLogged = settled;
    record(volts, rise);
    trial++;
}

void SweepChannel::record(double volts, const std::vector<Vector2>& rise) {
    char name[16];
    snprintf(name, sizeof name, "%s.txt", prefix);

    FILE* f = fopen(name, fileStarted ? "a" : "w");

    if (!f) {
        Brain.Screen.printAt(10, 30, false, "No SD - %.2fV lost", volts);
        return;
    }

    // The header goes down once, when the file is created. Changing drift or
    // window mid-session will not update it — delete the file and restart.
    if (!fileStarted) {
        fprintf(f, "drift %.4f\n", drift);
        fprintf(f, "window %d\n", window);
        fprintf(f, "unit %s\n", unit());
    }

    fprintf(f, "trial %.2f\n", volts);
    for (const Vector2& p : rise) fprintf(f, "(%.3f, %.3f)\n", p.x, p.y);

    fclose(f);
    fileStarted = true;
}

void SweepChannel::display() {
    // "Counterclockwise" does not fit the controller's 19 columns from cursor
    // 10, so the angular channel labels its directions CW / CCW.
    const char* dirLabel = axis == SweepAxis::Linear ? (positive ? "Forward" : "Backward")
                                                     : (positive ? "CW" : "CCW");

    Controller.Screen.clearScreen();

    Controller.Screen.setCursor(1, 1);
    Controller.Screen.print("Volts:");
    Controller.Screen.setCursor(1, 10);
    Controller.Screen.print("%.1fv", nextVolts);

    Controller.Screen.setCursor(2, 1);
    Controller.Screen.print("Dir:");
    Controller.Screen.setCursor(2, 10);
    Controller.Screen.print(dirLabel);

    Controller.Screen.setCursor(3, 1);
    Controller.Screen.print("MaxVel:");
    Controller.Screen.setCursor(3, 10);
    Controller.Screen.print("%.3f", lastLogged);

    // This is written to BE the body of controllerScreenTask, so it paces
    // itself — a bare while(true) around it would spin flat out.
    delay(SCREEN_MS);
}

bool sdCheck() {
    const char* NAME     = "sdtest.txt";
    const char* EXPECTED = "hello\nworld\n";

    Brain.Screen.clearScreen();
    Brain.Screen.setFont(mono20);

    auto fail = [](const char* why) {
        Brain.Screen.printAt(10, 150, false, "SD CHECK FAILED");
        Brain.Screen.printAt(10, 180, false, "%s", why);
        return false;
    };

    if (!Brain.SDcard.isInserted()) return fail("no card inserted");
    Brain.Screen.printAt(10, 30, false, "card: inserted");

    FILE* f = fopen(NAME, "w");
    if (!f) return fail("fopen w failed");
    int wrote = fprintf(f, "hello\n");
    fclose(f);
    Brain.Screen.printAt(10, 60, false, "write:  %d bytes", wrote);

    // The tuner appends every trial after the first, so that mode gets tested too.
    f = fopen(NAME, "a");
    if (!f) return fail("fopen a failed");
    int appended = fprintf(f, "world\n");
    fclose(f);
    Brain.Screen.printAt(10, 90, false, "append: %d bytes", appended);

    char buf[64] = {0};
    f = fopen(NAME, "r");
    if (!f) return fail("fopen r failed");
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';

    bool match = strcmp(buf, EXPECTED) == 0;
    Brain.Screen.printAt(10, 120, false, "read:   %d bytes %s", (int)n, match ? "match" : "MISMATCH");

    if (!match) return fail("bytes did not round trip");

    Brain.Screen.printAt(10, 150, false, "SD CHECK OK");
    Controller.rumble(".");
    return true;
}
