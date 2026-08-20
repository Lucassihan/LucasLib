#pragma once

// ramsete — include this one header to get the whole stack: spline → profile → schedule → tracking law → velocity layer.
// Both movements are members of Ramsete, not free functions: ramsete.moveTo(x, y) and ramsete.trajectory(path).

#include "ramsete/bezierTools.h"
#include "ramsete/velocityController.h"
#include "ramsete/config.h"
#include "ramsete/trajectory.h"
#include "ramsete/profile.h"
#include "ramsete/ramseteController.h"
