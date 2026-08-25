// moveToUtils.cpp — moveTo's math: the same law as the tracking controller, with
// the reference standing still instead of riding a schedule. Pure math, no vex.h,
// so the host checks can fly it the way they fly the law next door.

#include "ramsete/util/ramseteController.h"
#include "misc/util.h"

#include <cmath>

MoveToError Ramsete::moveToError(double targetX, double targetY,
                                 double robotX, double robotY, double robotH,
                                 bool driveBackward) {
    double dx = targetX - robotX;
    double dy = targetY - robotY;

    // Flipped 180° for a backward move, so everything below is measured from the
    // direction of TRAVEL rather than from the nose.
    double travel = TO_DEG(atan2(dx, dy));
    if (driveBackward) {
        travel += 180;
    }

    MoveToError error;
    error.heading  = wrapDegrees(travel - robotH);
    error.distance = hypot(dx, dy);
    if (driveBackward) {
        error.distance = -error.distance;
    }

    // SIGNED, not the magnitude. Driving tail-first reverses the drive command AND
    // swaps left/right; both flips live in this sign and have to cancel inside
    // moveToTurn's v·e_y product. fabs here inverts every backward move.
    error.crossTrack = error.distance * sin(TO_RAD(error.heading));
    return error;
}

// Volts, not deg/s like calculateTargetω — kLat's units are whatever make it so.
double Ramsete::moveToTurn(const MoveToError& e, double drivePower) {
    return kLat * drivePower * sinc(TO_RAD(e.heading)) * e.crossTrack;
}
