#pragma once

#include "ramsete/util/bezierUtils.h"

#include <vector>

// boomerangToUtils — building the path ramsete.boomerangTo drives.
// Pure geometry, taking the robot pose by argument rather than reading `chassis`, so the shape can
// be checked on the host the way bezierUtils and the law are.

// A cubic, plus a straight run onto the target whenever `padding` asks for one.
//
//   a = the robot,      handle leaving along `startHeading` for handleLength1
//   d = the PADDED end — (x, y) pulled back `padding` inches along endHeading — with its handle
//                       ARRIVING along `endHeading` for handleLength2
//
// The end handle points back UP the curve — c sits handleLength2 behind d along endHeading — which
// is what makes the robot arrive travelling in that direction rather than leaving in it.
//
// startHeading is the direction of TRAVEL, not the nose: on a backward move the caller passes the
// robot's heading + 180, or the curve sets off the way the robot is facing while the robot drives
// the other way. endHeading is used exactly as given.
//
// padding buys the last stretch as a LINE: the curve finishes early, on the padded point, and a
// second segment runs from there onto (x, y) with no bend left in it. The two meet tangent, so the
// bake sees a joint rather than a cusp and never slows for it. padding <= 0 returns the curve alone.
std::vector<BezierSegment> boomerangToPath(double robotX, double robotY, double startHeading,
                                           double x, double y, double endHeading,
                                           double handleLength1, double handleLength2,
                                           double padding = 0);
