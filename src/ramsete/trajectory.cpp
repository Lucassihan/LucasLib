// trajectory.cpp — reading the baked schedule. Pure math: no vex.h, no globals.

#include "ramsete/trajectory.h"

#include <cmath>

double wrapDegrees(double angle) {
    while (angle > 180)  angle -= 360;
    while (angle <= -180) angle += 360;
    return angle;
}

double trajectoryDuration(const std::vector<TrajPoint>& traj) {
    if (traj.empty()) {
        return 0;
    }
    return traj.back().t;
}

static TrajState blend(const TrajPoint& a, const TrajPoint& b, double u) {
    TrajState s;
    s.x       = a.x + (b.x - a.x) * u;
    s.y       = a.y + (b.y - a.y) * u;
    s.heading = a.heading + wrapDegrees(b.heading - a.heading) * u;
    s.v       = a.v + (b.v - a.v) * u;
    s.ω       = a.ω + (b.ω - a.ω) * u;
    return s;
}

TrajState sampleTrajectory(const std::vector<TrajPoint>& traj, double t) {
    if (traj.empty()) {
        return {};
    }
    int last = int(traj.size()) - 1;
    if (t <= traj[0].t or last == 0) {
        return blend(traj[0], traj[0], 0);
    }
    if (t >= traj[last].t) {
        return blend(traj[last], traj[last], 0);
    }

    // binary search for the row pair bracketing t
    int lo = 0;
    int hi = last;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (traj[mid].t <= t) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    double span = traj[hi].t - traj[lo].t;
    return blend(traj[lo], traj[hi], span > 1e-9 ? (t - traj[lo].t) / span : 0);
}
