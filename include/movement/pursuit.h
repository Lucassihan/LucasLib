#pragma once

#include "exitConditions.h"

#include <vector>

// =========================================================================
// Pure pursuit — follow a pre-planned path.
//
// The PATH is baked ahead of time by the debugger's "Pure Pursuit" planner:
// it densifies + smooths the waypoints and bakes a speed at every point, then
// emits the array below. At runtime followPath just chases it: each loop it
// aims at the point one lookahead ahead on the path and rides the arc to it.
// No planning happens on the robot.
// =========================================================================

// One baked point of a path: a position and the speed to hit there.
struct PathPoint {
    double x = 0;
    double y = 0;
    double v = 0;   // planned speed, in/s. The LAST point is baked to 0 — that is the end marker.
};

// Pure-pursuit tuning. Defaults are a reasonable VEX starting point.
struct PursuitParams {
    double lookahead = 12;     // chase-circle radius, inches (bigger = smoother, cuts corners more)
    double kV        = 0.18;   // in/s -> volts (about 12 V / free speed)
    double endSpeed  = 0;      // >0: reach the end still moving and hand off HOT to the next movement
};

// Follow a baked path by pure pursuit. Ends when the robot reaches the final
// (speed-0) point, or when `ec` fires. Honors the global `backward`: with it
// set, the robot follows the same path in reverse. With endSpeed > 0 the loop
// exits at the end WITHOUT stopping, so the next movement catches the pose hot.
void followPath(const std::vector<PathPoint>& path,
                PursuitParams pp = {},
                LateralExitConditions ec = {});
