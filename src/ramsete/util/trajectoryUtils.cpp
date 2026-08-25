// trajectoryUtils.cpp — reading the baked schedule. Pure math: no vex.h, no globals.

#include "ramsete/util/trajectoryUtils.h"

#include <cmath>

double wrapDegrees(double angle) {
    while (angle > 180)  angle -= 360;
    while (angle <= -180) angle += 360;
    return angle;
}

double trajectoryTailStart(const Trajectory& traj, double tail) {
    if (tail <= 0 or traj.size() < 2) {
        return -1;
    }

    std::size_t start = traj.size() - 1;
    double arc = 0;
    while (start > 0 and arc < tail) {
        arc += hypot(traj[start].x - traj[start - 1].x, traj[start].y - traj[start - 1].y);
        start--;
    }

    std::size_t peak = 0;
    for (std::size_t i = 1; i < traj.size(); i++) {
        if (fabs(traj[i].v) > fabs(traj[peak].v)) {
            peak = i;
        }
    }
    if (peak > start) {
        start = peak;
    }

    // The last row is the finish, not a handoff — there would be nothing left to drive.
    return start + 1 < traj.size() ? traj[start].t : -1;
}

double trajectoryDuration(const Trajectory& traj) {
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

TrajState sampleTrajectory(const Trajectory& traj, double t) {
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
