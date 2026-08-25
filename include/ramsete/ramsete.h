#pragma once

// ramsete — include this one header to get the whole stack: spline → profile → schedule → tracking law → velocity layer.
// Every movement is a member of Ramsete, not a free function: ramsete.moveTo(x, y), ramsete.trajectory(path), ramsete.boomerangTo(x, y, h, l1, l2, padding).

#include "ramsete/util/bezierUtils.h"
#include "ramsete/velocityController.h"
#include "ramsete/config.h"
#include "ramsete/util/trajectoryUtils.h"
#include "ramsete/util/profile.h"
#include "ramsete/util/ramseteController.h"
#include "ramsete/util/boomerangToUtils.h"
