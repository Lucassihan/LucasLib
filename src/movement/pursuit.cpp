#include "movement/pursuit.h"
#include "movement/movementUtils.h"   // motion globals, backward, clearAllValues, governOutputs
#include "movement/totalOutput.h"
#include "movement/odometry.h"
#include "misc/util.h"                 // voltageDrive, delay, TO_RAD
#include <cmath>
#include <algorithm>

////////////////////////////////////////////////////////////
//  PURE PURSUIT
//
//  Each loop does three things:
//    1. find the goal point  — one lookahead ahead on the path
//    2. steer  — ride the circular arc from the robot to the goal
//    3. speed  — use the baked speed at the closest path point
//  The path is followed until the robot reaches the final point.
////////////////////////////////////////////////////////////

// ---- goal-point search: pure geometry over the baked path ----

// The path point closest to the robot. Searched only in a WINDOW around the
// last closest index so a path that crosses itself can't snap to a far branch.
static int closestPoint(const std::vector<PathPoint>& path, int last, int window) {
    int    best     = last;
    double bestDist = 1e18;
    int    lo       = std::max(0, last - window);
    int    hi       = std::min((int)path.size() - 1, last + window);
    for (int i = lo; i <= hi; i++) {
        double d = hypot(path[i].x - chassis.x, path[i].y - chassis.y);
        if (d < bestDist) {
            bestDist = d;
            best     = i;
        }
    }
    return best;
}

// Where the lookahead circle (radius L, centered on the robot) crosses the
// segment a->b, returned as a fraction t in [0, 1]. Returns -1 if it misses.
// Solves the quadratic in t; the farther-along crossing is preferred.
static double circleHitsSegment(const PathPoint& a, const PathPoint& b, double L) {
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double fx = a.x - chassis.x;
    double fy = a.y - chassis.y;
    double qa = dx * dx + dy * dy;
    double qb = 2 * (fx * dx + fy * dy);
    double qc = fx * fx + fy * fy - L * L;
    double disc = qb * qb - 4 * qa * qc;
    if (disc < 0 or qa < 1e-9) return -1;
    disc = sqrt(disc);
    double t2 = (-qb + disc) / (2 * qa);
    if (t2 >= 0 and t2 <= 1) return t2;
    double t1 = (-qb - disc) / (2 * qa);
    if (t1 >= 0 and t1 <= 1) return t1;
    return -1;
}

// Fractional index of the goal point: the lookahead crossing farthest along the
// path, but never behind where the goal already was (monotonic, so it can't snap
// back on a hairpin). Within a lookahead of the end, chase the final point.
static double goalIndex(const std::vector<PathPoint>& path, double lastIndex, int closest, double L) {
    int start = std::max((int)lastIndex, closest);
    for (int i = start; i < (int)path.size() - 1; i++) {
        double t = circleHitsSegment(path[i], path[i + 1], L);
        if (t >= 0 and i + t >= lastIndex) return i + t;
    }
    const PathPoint& end = path.back();
    if (hypot(end.x - chassis.x, end.y - chassis.y) <= L) return path.size() - 1;
    return lastIndex;   // no crossing this loop — hold the last goal
}

// The path position at a fractional index (linear interpolation between points).
static PathPoint pathPointAt(const std::vector<PathPoint>& path, double index) {
    if (index >= path.size() - 1) return path.back();
    int    i = (int)index;
    double t = index - i;
    PathPoint p;
    p.x = path[i].x + (path[i + 1].x - path[i].x) * t;
    p.y = path[i].y + (path[i + 1].y - path[i].y) * t;
    return p;
}

// ---- the movement ----

void followPath(const std::vector<PathPoint>& path, PursuitParams pp, LateralExitConditions ec) {
    if (path.size() < 2) return;

    double startTime = Brain.timer(msec);
    clearAllValues();
    targetX = path.back().x;
    targetY = path.back().y;

    // Pursuit steers with its own arc mixing, so the governor's per-side min
    // floor (which would widen tight arcs) is turned off here and restored after.
    double savedMinOutput = totalOutput.minOutput;
    totalOutput.minOutput = 0;

    double trackwidth = chassis.getTrackwidth();
    int    closest    = 0;   // windowed closest index (where we ARE on the path)
    double goal       = 0;   // monotonic fractional goal index (where we AIM)
    bool   handoff    = false;

    while (not exitConditionFulfilled) {
        // 1. find the goal point one lookahead ahead on the path
        closest = closestPoint(path, closest, 25);
        goal = goalIndex(path, goal, closest, pp.lookahead);
        PathPoint aim = pathPointAt(path, goal);

        // 2. steer: curvature of the arc from the robot to the goal, in the robot
        // frame. `lat` is the goal's sideways offset; kappa = 2*lat / distance^2.
        // Positive kappa = goal to the right (CW+), so the left wheel runs faster.
        double heading = TO_RAD(backward ? chassis.h + 180 : chassis.h);
        double dx = aim.x - chassis.x;
        double dy = aim.y - chassis.y;
        double lat = dx * cos(heading) - dy * sin(heading);
        double dist2 = dx * dx + dy * dy;
        double kappa = (dist2 > 1e-9) ? 2 * lat / dist2 : 0;

        // 3. speed: the baked speed at the closest point, in/s -> volts. endSpeed
        // (if set) overrides the final point so the robot arrives still moving.
        bool atEnd = (closest == (int)path.size() - 1);
        double planned = path[closest].v;
        if (atEnd and pp.endSpeed > 0) planned = pp.endSpeed;
        double v = pp.kV * planned;

        // split that forward speed onto the two wheels for the arc, then govern
        // (the governor's max cap scales both sides together, keeping the arc).
        leftOut = v * (2 + kappa * trackwidth) / 2;
        rightOut = v * (2 - kappa * trackwidth) / 2;
        governOutputs();

        if (backward) voltageDrive(-rightOut, -leftOut);
        else          voltageDrive(leftOut, rightOut);

        // arrival: reaching the final point ends the path. A hot handoff
        // (endSpeed) breaks WITHOUT stopping so the next movement inherits the
        // motion; every other exit (timeout, user cross/settle) stops normally.
        lateralError = chassis.distanceTo(targetX, targetY, backward);
        if (atEnd) {
            handoff = (pp.endSpeed > 0);
            break;
        }
        exitConditionFulfilled = checkLateralExit(ec, startTime, lateralError, targetX, targetY, backward);

        delay(10);
    }

    totalOutput.minOutput = savedMinOutput;
    if (not handoff) voltageDrive(0, 0);
}


bool delay500ms() {
    delay(500);
    return true;
}

int task() {
    while (true) {
        if (delay500ms()) {
            bool execute = true;
        }
    }
}