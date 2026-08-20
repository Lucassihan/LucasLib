// profile.cpp — the 2D motion profile: ceilings, two passes, time integration.
// Reads its limits from the ramsete object; no vex.h, so it builds off the robot.

#include "ramsete/profile.h"
#include "ramsete/ramseteController.h"
#include "misc/util.h"

#include <cmath>
#include <algorithm>

// Below this speed the cruise branch of the time integration would divide by
// something near zero, so it borrows this floor instead. Only ever reached where
// the profile is already crawling (a cusp), where a hundredth of a second of
// timing error costs nothing.
static const double kMinIntegrationSpeed = 1.0;

// One place on the path, with everything the passes need to know about it.
struct ProfileSample {
    double x         = 0;
    double y         = 0;
    double heading   = 0;   // deg, already flipped for reversed segments
    double curvature = 0;   // rad/in, CW+
    double ceiling   = 0;   // in/s, the lowest limit that applies here
    double accel     = 0;   // in/s², this segment's budget
    double step      = 0;   // inches of ARC since the previous sample
    bool   reversed  = false;
};

// The spline parameters that split a segment into `count` pieces of EQUAL ARC.
// Stepping t uniformly does not do this — a cubic sweeps its parameter at wildly
// different speeds depending on its handles, and the long steps that produces
// land exactly where the curve bends hardest. Walk a fine table of arc length
// once, then read the parameters back out of it.
static std::vector<double> arcSpacedParams(const BezierSegment& seg, int count) {
    int fine = count * 8;
    std::vector<double> arc(fine + 1, 0.0);

    Pose last = bezierAt(seg, 0);
    for (int i = 1; i <= fine; i++) {
        Pose now = bezierAt(seg, double(i) / fine);
        arc[i] = arc[i - 1] + hypot(now.x - last.x, now.y - last.y);
        last = now;
    }

    std::vector<double> params(count + 1, 0.0);
    double total = arc[fine];
    int    walk  = 0;
    for (int j = 1; j <= count; j++) {
        double want = total * j / count;
        while (walk < fine and arc[walk + 1] < want) {
            walk++;
        }
        double span = arc[walk + 1] - arc[walk];
        double frac = span > 1e-9 ? (want - arc[walk]) / span : 0;
        params[j] = (walk + frac) / fine;
    }
    params[count] = 1.0;
    return params;
}

// Fraction of the motor's torque — so of the acceleration budget — still
// available at this fraction of free speed.
static double torqueFraction(double speedFraction, const ProfileConfig& config) {
    double left = 1.0 - config.torqueSlope * fabs(speedFraction);
    return std::max(left, config.torqueFloor);
}

static void resolveConfig(ProfileConfig& config) {
    if (config.maxVel     < 0) config.maxVel     = ramsete.maxVel;
    if (config.maxAccel   < 0) config.maxAccel   = ramsete.maxAccel;
    if (config.maxDecel   < 0) config.maxDecel   = config.maxAccel;
    if (config.trackwidth < 0) config.trackwidth = ramsete.trackwidth;
    if (config.dt         < 0) config.dt         = ramsete.sampleRate;
}

static int sampleCount(double length, double spacing) {
    return std::max(int(ceil(length / std::max(spacing, 0.25))), 5);
}

// Arc between the samples the two passes run on — an internal working resolution, not the output
// one. It only has to stay finer than a single output row at top speed, or the resample would be
// inventing rows by interpolating a grid coarser than itself.
static double passArcStep(const ProfileConfig& config) {
    double perRow = config.maxVel * (config.dt / 1000.0);
    return std::min(std::max(perRow / 2.0, 0.25), 1.0);
}

// Ceilings 1 and 2, evaluated at one sample.
static double speedCeiling(double curvature, const BezierSegment& seg, const ProfileConfig& config) {
    double top = config.maxVel * config.velMargin;

    if (seg.maxVel > 0) {
        top = std::min(top, seg.maxVel);
    }

    double bend = fabs(curvature);
    top = std::min(top, config.maxVel * config.velMargin / (1.0 + bend * config.trackwidth / 2.0));

    return top;
}

static std::vector<ProfileSample> sampleSpline(const std::vector<BezierSegment>& path,
                                               const ProfileConfig& config) {
    std::vector<ProfileSample> samples;

    for (size_t i = 0; i < path.size(); i++) {
        const BezierSegment& seg = path[i];

        double length = bezierLength(seg, 100);
        int    count  = sampleCount(length, passArcStep(config));
        double stride = length / count;

        std::vector<double> params = arcSpacedParams(seg, count);

        // Every segment samples its own t = 0, so a joint carries TWO rows at the
        // same place and the same timestamp: the outgoing segment's curvature and
        // the incoming one's. Handle-based splines are only C1 — curvature jumps
        // at a joint — and a single shared row would hand the first interval of
        // the new segment the OLD segment's turn rate, quietly losing half of the
        // heading change there. The duplicate row costs a zero-length interval.
        for (int j = 0; j <= count; j++) {
            double t = params[j];
            Pose   p = bezierAt(seg, t);

            ProfileSample s;
            s.x         = p.x;
            s.y         = p.y;
            s.heading   = bezierHeading(seg, t) + (seg.reversed ? 180.0 : 0.0);
            s.curvature = bezierCurvature(seg, t);
            s.accel     = seg.maxAccel > 0 ? seg.maxAccel : config.maxAccel;
            s.ceiling   = speedCeiling(s.curvature, seg, config);
            s.reversed  = seg.reversed;
            s.step      = j == 0 ? 0 : stride;
            samples.push_back(s);
        }

        // A cusp: the path doubles back, and the robot cannot flip direction
        // while moving. Pinning the ceiling to 0 is all it takes — the two passes
        // then brake into the joint and accelerate out of it on their own.
        bool flips = (i + 1 < path.size()) and (path[i + 1].reversed != seg.reversed);
        if (seg.stopEnd or flips) {
            samples.back().ceiling = 0;
        }
    }
    return samples;
}

// Position between two arc rows does NOT advance linearly in time. The passes put them a fixed
// DISTANCE apart, so wherever the robot is slow they sit far apart on the clock with the speed
// ramping hard across the gap — a straight time-lerp there freezes the step length while v climbs.
// Walking the same constant-acceleration integral the bake inverted puts the row where the robot
// actually is. Rates stay linear in time; only the distance along the pair is integrated.
static TrajPoint blendArc(const TrajPoint& a, const TrajPoint& b, double t) {
    double span = b.t - a.t;
    double tau  = t - a.t;
    double f    = span > 1e-9 ? tau / span : 0;

    double v0  = fabs(a.v);
    double v1  = fabs(b.v);
    double ds  = (v0 + v1) / 2.0 * span;
    double acc = span > 1e-9 ? (v1 - v0) / span : 0;

    double u = ds > 1e-9 ? (v0 * tau + 0.5 * acc * tau * tau) / ds : f;
    u = std::min(std::max(u, 0.0), 1.0);

    TrajPoint p;
    p.t       = t;
    p.x       = a.x + (b.x - a.x) * u;
    p.y       = a.y + (b.y - a.y) * u;
    p.heading = wrapDegrees(a.heading + wrapDegrees(b.heading - a.heading) * u);
    p.v       = a.v + (b.v - a.v) * f;
    p.ω       = a.ω + (b.ω - a.ω) * f;
    return p;
}

// The passes cannot run on a clock: v² = v₀² + 2a·Δs only closes over DISTANCE, and the time a row
// sits at is not known until both of them have finished. So the schedule is built on arc, then
// handed back on a clock — one row every config.dt.
//
// A joint's rows are never interpolated away, so the grid restarts at every one of them: the two
// rows there share a timestamp and carry the direction flip and the curvature step, and at a cusp
// that instant is the only place v is 0. Land beside it instead of on it and the schedule never
// actually stops turning around. Each joint therefore costs one short interval, as does the end.
static std::vector<TrajPoint> resampleByTime(const std::vector<TrajPoint>& arc, double dtSeconds) {
    if (arc.size() < 2 or dtSeconds < 1e-6) {
        return arc;
    }

    std::vector<TrajPoint> out;
    out.reserve(size_t(arc.back().t / dtSeconds) + arc.size() + 2);

    size_t start = 0;
    while (start + 1 < arc.size()) {
        size_t stop = start;
        while (stop + 1 < arc.size() and (arc[stop + 1].t - arc[stop].t) >= 1e-9) {
            stop++;
        }

        out.push_back(arc[start]);

        size_t seek = start;
        double t0   = arc[start].t;
        double t1   = arc[stop].t;
        for (int k = 1; t1 - (t0 + k * dtSeconds) >= 1e-9; k++) {
            double t = t0 + k * dtSeconds;
            while (seek + 1 < stop and arc[seek + 1].t < t) {
                seek++;
            }
            out.push_back(blendArc(arc[seek], arc[seek + 1], t));
        }

        if (stop > start) {
            out.push_back(arc[stop]);
        }
        start = stop + 1;
    }

    // The trailing half of a joint pair that lands on the very last row has no span of its own.
    if (start < arc.size()) {
        out.push_back(arc[start]);
    }
    return out;
}

std::vector<TrajPoint> bakeTrajectory(const std::vector<BezierSegment>& path, ProfileConfig config) {
    std::vector<TrajPoint> traj;
    if (path.empty()) {
        return traj;
    }

    resolveConfig(config);
    std::vector<ProfileSample> samples = sampleSpline(path, config);

    int count = int(samples.size());
    if (count < 2) {
        return traj;
    }

    // Step between consecutive samples, in inches of arc.
    std::vector<double> step(count, 0.0);
    for (int i = 1; i < count; i++) {
        step[i] = samples[i].step;
    }

    // Ceiling 3, forward: how fast can I be here given how fast I was there, and
    // given how much torque the motors still have at that speed.
    std::vector<double> speed(count, 0.0);
    speed[0] = std::min(config.startSpeed, samples[0].ceiling);
    for (int i = 1; i < count; i++) {
        double accel = samples[i].accel * torqueFraction(speed[i - 1] / config.maxVel, config);
        double grown = sqrt(speed[i - 1] * speed[i - 1] + 2.0 * accel * step[i]);
        speed[i] = std::min(samples[i].ceiling, grown);
    }

    // Ceiling 3, backward: the same sweep run end to beginning, which turns
    // "how fast can I accelerate away from the end" into "how early must I brake
    // toward it". Every decel ramp in the final profile comes from this pass.
    double finalSpeed = std::min(config.endSpeed, samples[count - 1].ceiling);
    speed[count - 1]  = std::min(speed[count - 1], finalSpeed);
    for (int i = count - 2; i >= 0; i--) {
        double braked = sqrt(speed[i + 1] * speed[i + 1] + 2.0 * config.maxDecel * step[i + 1]);
        speed[i] = std::min(speed[i], braked);
    }

    // Time integration: the profile becomes a trajectory. Ramps contribute
    // dv/a, cruises contribute dd/v.
    traj.resize(count);
    double clock = 0;
    for (int i = 0; i < count; i++) {
        if (i > 0 and step[i] > 1e-9) {
            double accel = (speed[i] * speed[i] - speed[i - 1] * speed[i - 1]) / (2.0 * step[i]);
            if (fabs(accel) > 0.1) {
                clock += (speed[i] - speed[i - 1]) / accel;
            } else {
                clock += step[i] / std::max(speed[i], kMinIntegrationSpeed);
            }
        }

        traj[i].t       = clock;
        traj[i].x       = samples[i].x;
        traj[i].y       = samples[i].y;
        traj[i].heading = wrapDegrees(samples[i].heading);
        traj[i].v       = samples[i].reversed ? -speed[i] : speed[i];

        // omega is derived, never authored, so it cannot drift out of agreement
        // with the curve being traced. The magnitude of the speed is what turns
        // the tangent — a reversed row spins the same way it would going forward.
        traj[i].ω = TO_DEG(speed[i] * samples[i].curvature);
    }
    return resampleByTime(traj, config.dt / 1000.0);
}
