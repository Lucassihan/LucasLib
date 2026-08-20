// executeTrajectory.cpp — the blocking movement that runs the law against a
// baked schedule. This is where ramsete/ meets the rest of LucasLib: odometry
// for the pose, the exit conditions for the safety bound, the debugger for the
// trace, and the velocity layer for the volts.

#include "ramsete/ramseteController.h"
#include "ramsete/profile.h"
#include "ramsete/velocityController.h"
#include "ramsete/config.h"
#include "movement/movementUtils.h"
#include "movement/odometry.h"
#include "coreConfigs.h"
#include "misc/util.h"
#include "debugger/debugger.h"

#include <cmath>

void Ramsete::trajectory(const std::vector<TrajPoint>& trajectory,
                         LateralExitConditions ec) {
    if (trajectory.size() < 2) {
        return;
    }

    double startTime = Brain.timer(msec);
    clearAllValues();
    velocityController.clearAllValues();
    omegaController.clearAllValues();

    const TrajPoint& finish = trajectory.back();
    double duration = trajectoryDuration(trajectory);

    // The exit conditions and the trace both work off the movement globals, so
    // point them at the schedule's last row.
    target.x = finish.x;
    target.y = finish.y;
    target.h = finish.heading;
    // cosineScaling false: the blend below runs with it OFF, and the trace should
    // say what the movement actually did, not what movePoint defaults to.
    debugger::movePoint("ramsete.trajectory", target.x, target.y, ec, finish.v < 0, false);
    debugger::trajectorySchedule(trajectory);

    // Direction is per-row inside a trajectory, but the cross-line exit reads the
    // global to know which way "past the target" points. Borrow it for the last
    // row's direction and hand it back untouched — nothing else should notice.
    bool wasBackward = backward;
    backward = finish.v < 0;

    double refClock  = 0;   // reference time — what the schedule is asked about
    double realClock = 0;   // real time, never scaled: its gap from refClock is the overrun
    double lastTick  = Brain.timer(msec);
    bool   scheduleDone = false;   // the clock reached the last row, vs an exit condition firing

    // The tick's working set lives out here so logRamsete() can publish it
    // without being handed a dozen arguments.
    TrajState    referenceState;
    RamseteError error;
    double k = 0;
    double rate = 1;
    double targetV = 0;
    double targetω = 0;

    auto logRamsete = [&] {
        ramsete.telemetry = {.refX = referenceState.x, .refY = referenceState.y,
                            .refHeading = referenceState.heading,
                            .refV = referenceState.v, .refω = referenceState.ω,
                            .errX = error.forward, .errY = error.crossTrack, .errHeading = error.heading,
                            .k = k, .clock = refClock, .realClock = realClock,
                            .clockRate = rate, .duration = duration,
                            .cmdV = targetV, .cmdω = targetω};

        debugger::probeTrajectory();
    };

    while (not exitConditionFulfilled) {
        double now = Brain.timer(msec);
        double dt  = (now - lastTick) / 1000.0;
        lastTick   = now;

        referenceState = sampleTrajectory(trajectory, refClock);
        error = ramsete.error(referenceState);
        k = ramsete.gain(referenceState);
        rate = ramsete.clockRate(error, referenceState, k);

        targetV = ramsete.calculateTargetV(referenceState, error, k, rate);
        targetω = ramsete.calculateTargetω(referenceState, error, k, rate);

        logRamsete();

        drivePow   = velocityController.calculateOutput(targetV);
        headingPow = omegaController.calculateOutput(targetω);

        blendOutputs({.cosineScaling = false, .turnPriority = 0});
        governOutputs();

        voltageDrive(leftOut, rightOut);

        refClock  += dt * rate;
        realClock += dt;

        // The settle exit measures against the schedule's last row.
        lateralError = hypot(finish.x - chassis.x, finish.y - chassis.y);

        // The schedule is the finish line. A braked clock means the robot gets
        // its whole path even after contact, and the timeout below is what bounds
        // the overrun if the clock never recovers.
        scheduleDone = refClock >= duration;
        exitConditionFulfilled = scheduleDone or checkLateralExit(ec, startTime);

        delay(10);
    }

    // Only the schedule's own ending is named here. checkLateralExit doesn't
    // report WHICH condition tripped, so if it was one of those, the reason
    // recorded at movement start (read off the armed conditions) stands.
    if (scheduleDone) {
        debugger::movementExit("schedule");
    }

    backward = wasBackward;

    // A schedule that ends at rest stops the motors; one baked with a nonzero
    // end speed hands the robot over still moving.
    if (fabs(finish.v) < 0.5) {
        voltageDrive(0, 0);
    }
}

void Ramsete::trajectory(const std::vector<BezierSegment>& path,
                         LateralExitConditions ec) {
    // The global drive direction applies to the whole path, the same way it flips
    // moveTo and boomerangTo: `backward = true` runs every segment nose-last.
    //
    // It XORs with a segment's own .reversed rather than overwriting it, so a
    // path that already changes direction partway keeps doing so — a .reversed
    // segment inside a backward path is the one driven nose-first, and the cusp
    // between them survives the flip.
    std::vector<BezierSegment> resolved = path;
    if (backward) {
        for (size_t i = 0; i < resolved.size(); i++) {
            resolved[i].reversed = not resolved[i].reversed;
        }
    }

    trajectory(bakeTrajectory(resolved), ec);
}
