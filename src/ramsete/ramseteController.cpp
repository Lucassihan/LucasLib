// ramseteController.cpp — the tracking law and the adaptive clock.

#include "ramsete/ramseteController.h"
#include "movement/odometry.h"
#include "misc/util.h"

#include <cmath>
#include <algorithm>

RamseteError Ramsete::error(const TrajState& referenceState) {
    double dx = referenceState.x - chassis.x;
    double dy = referenceState.y - chassis.y;
    double h  = TO_RAD(chassis.h);

    // The same field arrow, re-expressed in the robot's own axes. "3 inches
    // north" means nothing to a tank drive; "2 ahead, 1 right" is actionable.
    RamseteError error;
    error.forward = dx * sin(h) + dy * cos(h);
    error.crossTrack = dx * cos(h) - dy * sin(h);
    error.heading = wrapDegrees(referenceState.heading - chassis.h);
    return error;
}

double Ramsete::gain(const TrajState& referenceState) {
    // The formula wants rad/s, and the floor keeps the gain awake at the slow
    // ends of the profile, where v_d alone would zero it out.
    double omega = TO_RAD(referenceState.ω);
    double speed = std::max(fabs(referenceState.v), fabs(vMinGain));

    return 2.0 * zeta * sqrt(omega * omega + b * speed * speed);
}

double Ramsete::clockRate(const RamseteError& e, const TrajState& referenceState, double k) {
    double facing = fabs(cos(TO_RAD(e.heading)));

    // Extra speed the law is asking for right now, along the direction of
    // travel. On a reversed row the reference sits behind the nose, so `forward`
    // goes negative for the same lag — the direction sign puts it back.
    double direction = referenceState.v < 0 ? -1.0 : 1.0;
    double demand = direction * k * e.forward;

    double headroom = std::max(maxVel - fabs(referenceState.v), 0.0);
    double denom = std::max(k * fabs(referenceState.v) * facing, 1.0);

    // Zero while the robot keeps up; only once the demand outruns the headroom
    // does it start subtracting from the facing factor.
    double brake = clockCoupling * std::min((headroom - demand) / denom, 0.0);

    return std::max(facing + brake, 0.0);
}

double Ramsete::calculateTargetV(const TrajState& referenceState, const RamseteError& e,
                                 double k, double) {
    return referenceState.v * cos(TO_RAD(e.heading)) + k * e.forward;
}

double Ramsete::calculateTargetω(const TrajState& referenceState, const RamseteError& e,
                                 double k, double clockRate) {
    double heading = TO_RAD(e.heading);

    // Cross-track error has to be steered away — a tank drive can only erase a
    // sideways offset by moving, which is why that last term is scaled by v_d.
    double omega = TO_RAD(referenceState.ω) * clockRate
                 + k * heading
                 + b * referenceState.v * sinc(heading) * e.crossTrack;

    return TO_DEG(omega);
}

// =========================================================================
// moveTo's math. Same law, fixed target — see ramseteController.h.
// =========================================================================

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
