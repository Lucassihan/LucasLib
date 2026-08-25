// boomerangTo.cpp — one curve from wherever the robot is standing to a pose, driven
// like a trajectory. The whole movement is a shape and a delegation: build the
// path, bake it, hand it to the same follow loop ramsete.trajectory uses.

#include "ramsete/util/ramseteController.h"
#include "ramsete/util/boomerangToUtils.h"
#include "ramsete/util/profile.h"
#include "ramsete/util/trajectoryUtils.h"
#include "movement/movementUtils.h"
#include "movement/odometry.h"
#include "debugger/debugger.h"

void Ramsete::boomerangTo(double x, double y, double heading,
                          double handleLength1, double handleLength2, double padding,
                          PathSettings pathSettings, LateralExitConditions ec) {

    double startHeading = chassis.h + (backward ? 180 : 0);

    std::vector<BezierSegment> path = boomerangToPath(chassis.x, chassis.y, startHeading,
                                                      x, y, heading,
                                                      handleLength1, handleLength2, padding);

    Trajectory schedule = createTrajectory(path, pathSettings, backward);
    schedule.straightTail = padding;

    debugger::nextMoveType("ramsete.boomerangTo");
    trajectory(schedule, ec);
}
