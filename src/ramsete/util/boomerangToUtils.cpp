// boomerangToUtils.cpp — the path behind ramsete.boomerangTo. Pure geometry: no vex.h, no globals.

#include "ramsete/util/boomerangToUtils.h"
#include "misc/util.h"

#include <cmath>

std::vector<BezierSegment> boomerangToPath(double robotX, double robotY, double startHeading,
                                           double x, double y, double endHeading,
                                           double handleLength1, double handleLength2,
                                           double padding) {
    // (sin, cos) rather than (cos, sin): heading 0 is +Y here, so the unit vector
    // for an angle is the same one odometry drives the robot along.
    double sh = TO_RAD(startHeading);
    double ex = sin(TO_RAD(endHeading));
    double ey = cos(TO_RAD(endHeading));

    double px = x - padding * ex;
    double py = y - padding * ey;

    BezierSegment curve;
    curve.a = {robotX, robotY};
    curve.b = {robotX + handleLength1 * sin(sh), robotY + handleLength1 * cos(sh)};
    curve.c = {px - handleLength2 * ex, py - handleLength2 * ey};
    curve.d = {px, py};

    if (padding <= 0) {
        return {curve};
    }

    // Handles on the thirds. A cubic with evenly spaced control points is exactly the line, so the
    // tail carries zero curvature and a tangent that never degenerates the way a = b would.
    BezierSegment line;
    line.a = {px, py};
    line.b = {px + padding / 3 * ex, py + padding / 3 * ey};
    line.c = {px + padding * 2 / 3 * ex, py + padding * 2 / 3 * ey};
    line.d = {x, y};

    return {curve, line};
}
