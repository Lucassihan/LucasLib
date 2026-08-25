// trajectory.cpp — the blocking movement that runs the law against a
// baked schedule. This is where ramsete/ meets the rest of LucasLib: odometry
// for the pose, the exit conditions for the safety bound, the debugger for the
// trace, and the velocity layer for the volts.

#include "ramsete/util/ramseteController.h"
#include "ramsete/util/profile.h"
#include "ramsete/velocityController.h"
#include "ramsete/config.h"
#include "movement/movementUtils.h"
#include "movement/odometry.h"
#include "movement/pid.h"
#include "coreConfigs.h"
#include "misc/util.h"
#include "debugger/debugger.h"

#include <cmath>

// movement.cpp's stage boundary, shared so both point chases tune with one number.
extern double turnDisableLength;

void Ramsete::trajectory(const Trajectory& trajectory, LateralExitConditions ec) {
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
    // A reversed last row that ends at rest bakes to NEGATIVE zero, and (-0.0 < 0)
    // is false, so the sign bit is the only thing still carrying the direction
    // once the speed is gone. signbit reads it; a comparison throws it away.
    bool endsBackward = std::signbit(finish.v);
    bool endsAtRest   = fabs(finish.v) < 0.5;

    // A movement that delegates here — ramsete.boomerangTo — renames this record with
    // debugger::nextMoveType() rather than passing a trace argument down.
    debugger::movePoint("ramsete.trajectory", target.x, target.y, ec, endsBackward, false);
    debugger::trajectorySchedule(trajectory);

    // Direction is per-row inside a trajectory, but the cross-line exit reads the
    // global to know which way "past the target" points. Borrow it for the last
    // row's direction and hand it back untouched — nothing else should notice.
    bool wasBackward = backward;
    backward = endsBackward;

    // A hot exit is meant to hand the robot over still moving, so there is nothing
    // to park; neither handoff is ever for a schedule that ends stopped.
    double finalDistance = trajectory.finalDistance;
    double tailStart     = trajectoryTailStart(trajectory, trajectory.straightTail);

    // The tail owns the last inches outright, so the point chase stays out of them — two handoffs
    // into the same stretch would fight over the same two PIDs.
    bool tailArmed  = endsAtRest and tailStart >= 0;
    bool chaseArmed = endsAtRest and finalDistance > 0 and not tailArmed;
    bool chasing    = false;
    bool tailing    = false;

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

    // Seeded off the MEASUREMENT, not zero: a schedule baked with an end speed hands the robot
    // over still moving, and a slew starting from rest would brake it on the first tick.
    double lastCmdV = chassis.v;
    double lastCmdω = TO_DEG(chassis.ω);

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

        if (tailing) {
            // driveForward's body, with the schedule's last row standing in for the heading it
            // captures and the target point standing in for its tracked distance.
            headingError = chassis.wrappedHeadingError(target.h);

            // The distance left to (target.x, target.y), taken ALONG the held heading rather than
            // as a hypot. On the line the two are the same number, but only this one goes negative
            // once the robot is past the target, and a distance that cannot go negative is one the
            // PID can never brake against. The offset runs along travel, as it does everywhere else.
            double heldHeading = TO_RAD(target.h);
            lateralError = (target.x - chassis.x) * sin(heldHeading)
                         + (target.y - chassis.y) * cos(heldHeading)
                         + (backward ? -drivePID.targetOffset : drivePID.targetOffset);

            drivePow   = drivePID.calculateOutput(lateralError);
            headingPow = headingPID.calculateOutput(headingError);

            // Same as the chase: the reference is parked, so publish it standing
            // still rather than dropping the columns.
            rate = 0;
            targetV = 0;
            targetω = 0;
            error = ramsete.error(referenceState);
            logRamsete();
        } else if (not chasing) {
            referenceState = sampleTrajectory(trajectory, refClock);
            error = ramsete.error(referenceState);
            k = ramsete.gain(referenceState);
            rate = ramsete.clockRate(error, referenceState, k);

            targetV = ramsete.calculateTargetV(referenceState, error, k, rate);
            targetω = ramsete.calculateTargetω(referenceState, error, k, rate);

            // Last layer. The 2DMP already respected its own limits; this one answers for
            // everything the law added afterward, so it is applied to the finished command and
            // nothing upstream is told about it.
            targetV = slew(targetV, lastCmdV, ramsete.vMaxAccelRate * dt, ramsete.vMaxDecelRate * dt);
            targetω = slew(targetω, lastCmdω, ramsete.ωMaxAccelRate * dt, ramsete.ωMaxDecelRate * dt);
            lastCmdV = targetV;
            lastCmdω = targetω;

            logRamsete();

            drivePow   = velocityController.calculateOutput(targetV);
            headingPow = omegaController.calculateOutput(targetω);
        } else {
            // moveTo's body, inlined. Calling the movement would restart its own
            // loop, clear the state this one is mid-way through, and re-tag the
            // trace as a second movement — all this phase wants is the law.
            MoveToError chaseError = ramsete.moveToError(target.x, target.y,
                                                         chassis.x, chassis.y, chassis.h, backward);

            if (not close) {
                headingError = chaseError.heading;
                lateralError = chaseError.distance + (backward ? -drivePID.targetOffset : drivePID.targetOffset);

                if (fabs(chaseError.distance) < turnDisableLength) {
                    close = true;
                    target.h = chassis.h;
                    targetStraightDistance = lateralError;
                    chassis.clearTrackedInches();
                }
            }

            if (close) {
                headingError = chassis.wrappedHeadingError(target.h);
                lateralError = targetStraightDistance - chassis.yInchesTracked;
            }

            drivePow   = cos(TO_RAD(headingError)) * drivePID.calculateOutput(lateralError);
            headingPow = headingPID.calculateOutput(headingError)
                       + (not close ? ramsete.moveToTurn(chaseError, drivePow) : 0);

            // The reference stopped moving, so publish it standing still rather
            // than dropping the columns: the ghost parks on the last row and a
            // clock rate of zero is what says the schedule is no longer running.
            rate = 0;
            targetV = 0;
            targetω = 0;
            error = ramsete.error(referenceState);
            logRamsete();
            debugger::probeRamseteMove(chaseError.crossTrack);
        }

        // The tail is a driveForward, down to how it splits the volts; the schedule stages steer
        // on the law and take neither the cosine nor the turn priority.
        blendOutputs(tailing ? BlendSettings{} : BlendSettings{.cosineScaling = false, .turnPriority = 0});
        governOutputs();

        voltageDrive(leftOut, rightOut);

        refClock  += dt * rate;
        realClock += dt;

        if (chasing or tailing) {
            // lateralError is the stage's own, set above — leave it alone. The
            // movement's own exit conditions carry over; the schedule does not.
            exitConditionFulfilled = checkLateralExit(ec, startTime);
        } else {
            // The settle exit measures against the schedule's last row.
            lateralError = hypot(finish.x - chassis.x, finish.y - chassis.y);

            // Close enough that the schedule has nothing left to teach, and the
            // law behind it cannot hold a position. The REFERENCE has to be in the
            // endzone as well, not just the robot: a route that doubles back past
            // its own finish would otherwise hand off on the flyby and abandon the
            // rest of the path.
            double refToFinish = hypot(finish.x - referenceState.x, finish.y - referenceState.y);

            // The REFERENCE reaching the tail is the whole trigger — the schedule is what knows
            // where the curve stopped bending, and the robot behind it is exactly what the PIDs
            // are being handed the job of closing. The clock says when, because the row that
            // answers it was picked at bake time and the clock is what walks the rows.
            if (tailArmed and refClock >= tailStart) {
                tailing = true;
            } else if (chaseArmed and lateralError < finalDistance and refToFinish < finalDistance) {
                chasing = true;
            }

            // The schedule is the finish line. A braked clock means the robot gets
            // its whole path even after contact, and the timeout below is what bounds
            // the overrun if the clock never recovers. Handing off outranks it: the
            // tick that opens the gate must not also end the movement.
            bool handedOff = chasing or tailing;
            scheduleDone = not handedOff and refClock >= duration;
            exitConditionFulfilled = scheduleDone or (not handedOff and checkLateralExit(ec, startTime));
        }

        delay(10);
    }

    // Only the schedule's own ending is named here. checkLateralExit doesn't
    // report WHICH condition tripped, so if it was one of those, the reason
    // recorded at movement start (read off the armed conditions) stands.
    if (tailing) {
        debugger::movementExit("padding");
    } else if (chasing) {
        debugger::movementExit("chase");
    } else if (scheduleDone) {
        debugger::movementExit("schedule");
    }

    backward = wasBackward;
    if (chasing or tailing) {
        drivePID.targetOffset = 0;
    }

    // A schedule that ends at rest stops the motors; one baked with a nonzero
    // end speed hands the robot over still moving. A chase only runs on the
    // former, so it always stops.
    if (endsAtRest) {
        voltageDrive(0, 0);
    }
}
