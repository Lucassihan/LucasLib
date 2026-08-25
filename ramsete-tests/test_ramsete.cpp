// Host-side checks for LucasLib's ramsete math layers (bezier, profile,
// trajectory, ramseteController). Compiled natively — no vex.h anywhere in that
// dependency set, which is the point.

#include "ramsete/util/bezierUtils.h"
#include "ramsete/util/profile.h"
#include "ramsete/util/trajectoryUtils.h"
#include "ramsete/util/ramseteController.h"
#include "ramsete/util/boomerangToUtils.h"
#include "movement/odometry.h"

#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

Ramsete ramsete;   // normally lives in config.cpp
bool backward = false;   // movementUtils' global drive direction, read by createTrajectory
Odometry chassis;  // the law reads the tracked pose straight off this

// The values initializeRamseteConstants() would set on the robot. The harness
// has to do it itself, since config.cpp needs vex and is not built here.
static void configureRamsete() {
    ramsete.trackwidth = 13.5;
    ramsete.maxVel     = 55.0;
    ramsete.maxAccel   = 90.0;
    ramsete.sampleRate = 10.0;
    ramsete.b          = 0.03;
    ramsete.zeta       = 0.4;
    ramsete.vMinGain   = 4.0;
    ramsete.clockCoupling = 6.0;
    ramsete.kLat          = 0.02;
}

static int failures = 0;
static void check(bool ok, const char* what, double value = 0) {
    if (!ok) { printf("  FAIL  %s   (value %.4f)\n", what, value); failures++; }
    else     { printf("  ok    %s   (%.4f)\n", what, value); }
}

static const double DEG = M_PI / 180.0;

static std::vector<BezierSegment> demoPath() {
    BezierSegment s1; s1.a = {26, 18};  s1.b = {26, 62};   s1.c = {34, 66};  s1.d = {64, 86};
    BezierSegment s2; s2.a = {64, 86};  s2.b = {86, 101};  s2.c = {126, 70}; s2.d = {118, 118};
    return {s1, s2};
}

// De Casteljau split of segment 0 at t = 0.5. The two halves trace the SAME
// curve as the original — which is what makes "split it, then cap one piece"
// a free way to slow a span inside a curve.
static std::vector<BezierSegment> splitFirstInHalf(std::vector<BezierSegment> path) {
    auto mid = [](const Pose& p, const Pose& q) {
        Pose r; r.x = (p.x + q.x) / 2; r.y = (p.y + q.y) / 2; return r;
    };
    BezierSegment s = path[0];
    Pose l1 = mid(s.a, s.b), hh = mid(s.b, s.c), r2 = mid(s.c, s.d);
    Pose l2 = mid(l1, hh),   r1 = mid(hh, r2);
    Pose m  = mid(l2, r1);

    BezierSegment first  = s; first.a  = s.a; first.b  = l1; first.c = l2; first.d = m;
    BezierSegment second = s; second.a = m;   second.b = r1; second.c = r2; second.d = s.d;

    path[0] = second;
    path.insert(path.begin(), first);
    return path;
}

// ---------------------------------------------------------------- profile ---
static void testProfile() {
    printf("\n[1] profile: ceilings, passes, integration\n");
    PathSettings cfg;
    cfg.maxVel = 55; cfg.maxAccel = 90; cfg.trackwidth = 13.5;
    Trajectory traj = createTrajectory(demoPath(), cfg);

    check(traj.size() > 20, "row count", traj.size());

    bool monotonic = true, ceilingOK = true, accelOK = true, omegaOK = true;
    double worstAccel = 0, worstCeil = 0;
    for (size_t i = 1; i < traj.size(); i++) {
        // Time never goes backwards. Two rows MAY share a timestamp, but only at
        // a segment joint, where they also share a position.
        if (traj[i].t < traj[i - 1].t) monotonic = false;
        if (traj[i].t == traj[i - 1].t &&
            hypot(traj[i].x - traj[i-1].x, traj[i].y - traj[i-1].y) > 1e-6) monotonic = false;
        double dd = hypot(traj[i].x - traj[i-1].x, traj[i].y - traj[i-1].y);
        double dt = traj[i].t - traj[i-1].t;
        if (dt > 1e-6) {
            double a = (fabs(traj[i].v) - fabs(traj[i-1].v)) / dt;
            worstAccel = std::max(worstAccel, fabs(a));
            if (fabs(a) > cfg.maxAccel * 1.05) accelOK = false;
            // the rows' own geometry must agree with the speed they claim
            double implied = dd / dt;
            double avg = (fabs(traj[i].v) + fabs(traj[i-1].v)) / 2;
            if (avg > 2 && fabs(implied - avg) > 0.25 * avg) omegaOK = false;
        }
        double vcap = cfg.maxVel * cfg.velMargin;
        if (fabs(traj[i].v) > vcap + 1e-6) { ceilingOK = false; worstCeil = fabs(traj[i].v); }
    }
    check(monotonic, "time never goes backwards; ties only at joints");
    check(ceilingOK, "speed never exceeds margin*maxVel", worstCeil);
    check(accelOK, "acceleration inside the budget", worstAccel);
    check(omegaOK, "row spacing agrees with row speed");
    check(fabs(traj.front().v) < 1e-9, "starts at rest", traj.front().v);
    check(fabs(traj.back().v) < 1e-9, "ends at rest", traj.back().v);
    check(traj.back().t > 1.0 && traj.back().t < 12.0, "plausible duration (s)", traj.back().t);

    // curvature cap: the outer wheel must stay under free speed everywhere
    bool wheelOK = true; double worstWheel = 0;
    for (size_t i = 1; i < traj.size(); i++) {
        double dt = traj[i].t - traj[i-1].t;
        if (dt < 1e-6) continue;
        double omega = traj[i].ω * DEG;                 // rad/s
        double outer = fabs(traj[i].v) + fabs(omega) * cfg.trackwidth / 2;
        worstWheel = std::max(worstWheel, outer);
        if (outer > cfg.maxVel + 0.5) wheelOK = false;
    }
    check(wheelOK, "outer wheel stays under free speed", worstWheel);
}

// ------------------------------------------------------------ feasibility ---
// The strongest check on the frame conventions: drive the unicycle model with
// ONLY the schedule's own (v, omega) and see whether it reproduces the rows.
static void testFeasibility() {
    printf("\n[2] feasibility: integrating (v, omega) must retrace the rows\n");
    Trajectory traj = createTrajectory(demoPath(), {});
    double dt = 0.002, worst = 0, worstH = 0;
    double x = traj[0].x, y = traj[0].y, h = traj[0].heading;

    for (double t = 0; t < trajectoryDuration(traj); t += dt) {
        TrajState r = sampleTrajectory(traj, t);
        x += r.v * sin(h * DEG) * dt;
        y += r.v * cos(h * DEG) * dt;
        h += r.ω * dt;
        worst  = std::max(worst, hypot(r.x - x, r.y - y));
        worstH = std::max(worstH, fabs(wrapDegrees(r.heading - h)));
    }
    // The floor here is the spline's own G1 joint kink (~0.6°), which no finite
    // omega can integrate through — inherent to handle-based cubics, not to us.
    check(worst < 1.0, "integrated position tracks the rows (in)", worst);
    check(worstH < 2.0, "integrated heading tracks the rows (deg)", worstH);
}

// ------------------------------------------------------------------ cusps ---
static void testCusp() {
    printf("\n[3] cusp: a direction flip forces v = 0\n");
    BezierSegment s1; s1.a = {72, 24}; s1.b = {72, 48}; s1.c = {72, 60}; s1.d = {72, 72};
    BezierSegment s2; s2.a = {72, 72}; s2.b = {72, 60}; s2.c = {72, 48}; s2.d = {72, 24};
    s2.reversed = true;
    Trajectory traj = createTrajectory({s1, s2}, {});

    double minMid = 1e9; size_t at = 0;
    for (size_t i = 0; i < traj.size(); i++) {
        if (traj[i].y > 71.0 && fabs(traj[i].v) < minMid) { minMid = fabs(traj[i].v); at = i; }
    }
    check(minMid < 1e-6, "speed passes through zero at the turnaround", minMid);
    check(traj[at + 2].v < 0, "the second segment is driven backward", traj[at + 2].v);
    // heading must NOT jump: the path doubles back, so tangent+180 == the old tangent
    double jump = fabs(wrapDegrees(traj[at + 1].heading - traj[at - 1].heading));
    check(jump < 5.0, "heading is continuous across the cusp (deg)", jump);
}

// ---------------------------------------------------------------- the law ---
struct Sim { double x, y, h, v, w; };

// One closed-loop run. Returns the worst cross-track error after the first
// half-second of settling, and reports where it finished.
static Sim runSim(const Trajectory& traj, bool adaptive, Sim bot,
                  double* worstLate, double shoveAt = -1, bool print = false) {
    const double dt = 0.01, tau = 0.09;
    double clock = 0, wall = 0;
    *worstLate = 0;
    double duration = trajectoryDuration(traj);

    while (clock < duration && wall < 25.0) {
        TrajState referenceState = sampleTrajectory(traj, clock);
        chassis = {bot.x, bot.y, bot.h};
        RamseteError error = ramsete.error(referenceState);
        double k = ramsete.gain(referenceState);
        double rate = adaptive ? ramsete.clockRate(error, referenceState, k) : 1.0;
        double cmdV = ramsete.calculateTargetV(referenceState, error, k, rate);
        double cmdω = ramsete.calculateTargetω(referenceState, error, k, rate);

        double vSat = std::max(-ramsete.maxVel, std::min(ramsete.maxVel, cmdV));
        bot.v += (vSat - bot.v) * (dt / tau);
        bot.w += (cmdω - bot.w) * (dt / tau);
        bot.x += bot.v * sin(bot.h * DEG) * dt;
        bot.y += bot.v * cos(bot.h * DEG) * dt;
        bot.h += bot.w * dt;

        if (wall > 0.5) *worstLate = std::max(*worstLate, hypot(error.forward, error.crossTrack));
        if (shoveAt > 0 && wall < shoveAt && wall + dt >= shoveAt) {
            bot.x += 10 * cos(bot.h * DEG);   // 10" to the robot's right
            bot.y -= 10 * sin(bot.h * DEG);
            if (print) printf("        (shoved 10\" sideways at t=%.2f)\n", wall);
        }
        clock += dt * rate;
        wall  += dt;
    }
    if (print) printf("        finished wall=%.2fs at (%.1f, %.1f) h=%.1f\n", wall, bot.x, bot.y, bot.h);
    return bot;
}

static void testLaw() {
    printf("\n[4] the law: convergence from a clean start and from an offset\n");
    Trajectory traj = createTrajectory(demoPath(), {});
    const TrajPoint& end = traj.back();
    double worst;

    Sim clean = runSim(traj, true, {traj[0].x, traj[0].y, traj[0].heading, 0, 0}, &worst, -1, true);
    // The sim gives the drivetrain a 90 ms first-order lag, so the peak error
    // lands during the opening ramp. 2.4" is what the site's own demo reports.
    check(worst < 3.0, "clean start: worst tracking error (in)", worst);
    check(hypot(end.x - clean.x, end.y - clean.y) < 3.0, "clean start: finishes on the last row (in)",
          hypot(end.x - clean.x, end.y - clean.y));

    Sim off = runSim(traj, true, {traj[0].x - 8, traj[0].y - 4, traj[0].heading + 30, 0, 0}, &worst, -1, true);
    check(hypot(end.x - off.x, end.y - off.y) < 4.0, "8\"/30° offset start: still finishes on the schedule (in)",
          hypot(end.x - off.x, end.y - off.y));
    check(fabs(wrapDegrees(end.heading - off.h)) < 15.0, "offset start: heading converges (deg)",
          fabs(wrapDegrees(end.heading - off.h)));

    printf("\n[5] the clock: same shove, fixed vs adaptive\n");
        double worstFixed, worstAdaptive;
    Sim f = runSim(traj, false, {traj[0].x, traj[0].y, traj[0].heading, 0, 0}, &worstFixed, 1.0, true);
    Sim a = runSim(traj, true,       {traj[0].x, traj[0].y, traj[0].heading, 0, 0}, &worstAdaptive, 1.0, true);
    printf("        worst error after the shove: fixed %.2f\"  adaptive %.2f\"\n", worstFixed, worstAdaptive);
    check(worstAdaptive <= worstFixed + 0.01, "adaptive clock tracks the shove no worse than fixed",
          worstFixed - worstAdaptive);
    check(hypot(end.x - a.x, end.y - a.y) < 4.0, "adaptive: recovers and finishes (in)",
          hypot(end.x - a.x, end.y - a.y));
    check(hypot(end.x - f.x, end.y - f.y) < 6.0, "fixed: recovers and finishes (in)",
          hypot(end.x - f.x, end.y - f.y));

    printf("\n[6] reversed rows: the same law backs the robot down a path\n");
    BezierSegment b; b.a = {72, 96}; b.b = {72, 72}; b.c = {60, 60}; b.d = {48, 48};
    b.reversed = true;
    Trajectory back = createTrajectory({b}, {});
    double travel = atan2(back[1].x - back[0].x, back[1].y - back[0].y) / DEG;
    check(fabs(fabs(wrapDegrees(travel - back.front().heading)) - 180) < 5.0,
          "reversed row faces 180° away from travel (deg)", wrapDegrees(travel - back.front().heading));
    Sim r = runSim(back, true, {back[0].x, back[0].y, back[0].heading, 0, 0}, &worst, -1, true);
    check(worst < 2.5, "reversed: worst tracking error (in)", worst);
    check(hypot(back.back().x - r.x, back.back().y - r.y) < 3.0, "reversed: finishes on the last row (in)",
          hypot(back.back().x - r.x, back.back().y - r.y));
}

// ---------------------------------------------------------- gains + edges ---
static void testGainAndEdges() {
    printf("\n[7] gain schedule and edge cases\n");
    TrajState fast; fast.v = 60; fast.ω = 0;
    TrajState slow; slow.v = 10; slow.ω = 0;
    TrajState stop; stop.v = 0;  stop.ω = 0;
    double kf = ramsete.gain(fast), ks = ramsete.gain(slow), k0 = ramsete.gain(stop);
    printf("        k(60 in/s)=%.2f  k(10 in/s)=%.2f  k(0)=%.2f\n", kf, ks, k0);
    check(kf > ks && ks > 0, "gain rides the referenceState speed", kf / ks);
    check(k0 > 0.1, "vMin floor keeps the gain alive at standstill", k0);

    Trajectory empty;
    check(trajectoryDuration(empty) == 0, "empty trajectory has zero duration");
    TrajState s = sampleTrajectory(empty, 3.0);
    check(s.v == 0, "sampling an empty trajectory is harmless");

    Trajectory one = createTrajectory({}, {});
    check(one.empty(), "baking an empty path yields no rows");

    Trajectory traj = createTrajectory(demoPath(), {});
    TrajState before = sampleTrajectory(traj, -5);
    TrajState after  = sampleTrajectory(traj, 999);
    check(fabs(before.x - traj.front().x) < 1e-9, "clock before 0 clamps to the first row");
    check(fabs(after.x  - traj.back().x)  < 1e-9, "clock past the end clamps to the last row");

    // hot entry / hot exit
    PathSettings hot; hot.startSpeed = 20; hot.endSpeed = 15;
    Trajectory h = createTrajectory(demoPath(), hot);
    check(fabs(h.front().v - 20) < 1e-6, "startSpeed lands in the first row", h.front().v);
    check(fabs(h.back().v - 15) < 1e-6, "endSpeed lands in the last row", h.back().v);

    // authored per-segment cap
    std::vector<BezierSegment> capped = demoPath();
    capped[1].maxVel = 12;
    Trajectory c = createTrajectory(capped, {});
    size_t joint = 0; double best = 1e9;
    for (size_t i = 0; i < c.size(); i++) {
        double d = hypot(c[i].x - capped[1].a.x, c[i].y - capped[1].a.y);
        if (d < best) { best = d; joint = i; }
    }
    double maxSecond = 0;
    for (size_t i = joint + 1; i < c.size(); i++) maxSecond = std::max(maxSecond, fabs(c[i].v));
    check(maxSecond <= 12.001, "authored per-segment cap is respected (in/s)", maxSecond);

    // .maxVel is the only route-authored ceiling now, and it applies to a WHOLE
    // segment — so capping a span inside a curve means splitting it first. That
    // only works if a split is free: de Casteljau reproduces the same curve, and
    // an ordinary joint carries no speed penalty (only .stopEnd and a direction
    // flip pin a row to zero). Split segment 0 in half and the schedule should
    // come out the same.
    std::vector<BezierSegment> split = splitFirstInHalf(demoPath());
    check(split.size() == 3, "de Casteljau split adds one segment", split.size());

    double dWhole = trajectoryDuration(createTrajectory(demoPath(), {}));
    double dSplit = trajectoryDuration(createTrajectory(split, {}));
    check(fabs(dSplit - dWhole) < 0.02, "splitting a segment does not change the schedule (s)", dSplit - dWhole);

    // ...and the split is what makes a sub-segment cap possible: slow only the
    // first half and the second half keeps its speed.
    split[0].maxVel = 10;
    Trajectory half = createTrajectory(split, {});
    double firstHalfMax = 0, laterMax = 0;
    for (size_t i = 0; i < half.size(); i++) {
        double d = hypot(half[i].x - split[0].d.x, half[i].y - split[0].d.y);
        // rows before the split point belong to the capped piece
        if (half[i].t < 0.5 * trajectoryDuration(half) and d > 1.0) {
            firstHalfMax = std::max(firstHalfMax, fabs(half[i].v));
        }
        laterMax = std::max(laterMax, fabs(half[i].v));
    }
    check(firstHalfMax <= 10.001, "a split lets one span be capped alone (in/s)", firstHalfMax);
    check(laterMax > 15.0, "the uncapped remainder still runs fast (in/s)", laterMax);
}

// A defender pins the robot for a second: nothing it commands moves it. The
// question is where the reference is when it gets free.
static void testPin() {
    printf("\n[8] the pin: where is the referenceState when the robot gets free?\n");
    Trajectory traj = createTrajectory(demoPath(), {});
    const double dt = 0.01, tau = 0.09;

    for (int adaptive = 0; adaptive < 2; adaptive++) {
        Sim bot = {traj[0].x, traj[0].y, traj[0].heading, 0, 0};
        double clock = 0, wall = 0, gapAtRelease = 0, minRate = 1;

        while (clock < trajectoryDuration(traj) && wall < 25.0) {
            TrajState referenceState = sampleTrajectory(traj, clock);
            chassis = {bot.x, bot.y, bot.h};
            RamseteError error = ramsete.error(referenceState);
            double k = ramsete.gain(referenceState);
            double rate = adaptive ? ramsete.clockRate(error, referenceState, k) : 1.0;
            double cmdV = ramsete.calculateTargetV(referenceState, error, k, rate);
        double cmdω = ramsete.calculateTargetω(referenceState, error, k, rate);
            minRate = std::min(minRate, rate);

            bool pinned = wall > 1.0 && wall < 2.0;
            if (!pinned) {
                double vSat = std::max(-ramsete.maxVel, std::min(ramsete.maxVel, cmdV));
                bot.v += (vSat - bot.v) * (dt / tau);
                bot.w += (cmdω - bot.w) * (dt / tau);
                bot.x += bot.v * sin(bot.h * DEG) * dt;
                bot.y += bot.v * cos(bot.h * DEG) * dt;
                bot.h += bot.w * dt;
            } else {
                bot.v = 0; bot.w = 0;
            }
            if (wall >= 2.0 && wall < 2.0 + dt) gapAtRelease = hypot(error.forward, error.crossTrack);
            clock += dt * rate;
            wall  += dt;
        }
        printf("        %-8s the reference was %5.1f\" away at release; slowest clock rate %.2f; wall %.2fs\n",
               adaptive ? "adaptive" : "fixed", gapAtRelease, minRate, wall);
        if (adaptive) {
            check(gapAtRelease < 20.0, "adaptive: the reference waited nearby (in)", gapAtRelease);
            check(minRate < 0.5, "adaptive: the clock actually braked", minRate);
            check(hypot(traj.back().x - bot.x, traj.back().y - bot.y) < 4.0,
                  "adaptive: still finishes on the schedule (in)",
                  hypot(traj.back().x - bot.x, traj.back().y - bot.y));
        } else {
            check(gapAtRelease > 25.0, "fixed: the reference drove away, as advertised (in)", gapAtRelease);
        }
    }
}

// ----------------------------------------------------------------- moveTo ---
// Stand-in for drivePID / headingPID. The harness cannot link the real PID class
// (it reaches vex.h), and what is under test here is the law's geometry and its
// signs, not the PID's arithmetic.
struct SimplePID {
    double kP, kD, maxOut;
    double last = 0;
    bool   first = true;
    // dtMillis, not seconds: PID::timeSinceLastCycle() reports milliseconds, so
    // the real kD values in auton.cpp are calibrated against a number ~1000x
    // larger than dt in seconds. Feeding seconds here makes kD 1000x too strong.
    double out(double e, double dtMillis) {
        if (first) { last = e; first = false; }
        double d = (e - last) / dtMillis;
        last = e;
        double raw = kP * e + kD * d;
        return std::max(-maxOut, std::min(maxOut, raw));
    }
};

// Flies the REAL moveToError and moveToTurn through a unicycle, mixed exactly the
// way moveTo.cpp mixes them. Anything wrong with the sign composition shows up
// here as a robot that curves away from the target instead of onto it.
// `settle` mirrors checkLateralExit's speed gate at exitConditions.h's defaults.
// Turning it OFF is the honest test of the control law: with only a timeout, a
// stable movement still has to converge on the target and park there. A settle
// that fires while the robot is merely passing through zero speed on its way
// somewhere else is a false positive, and would hide exactly that failure.
static Sim runMoveTo(Sim bot, double tx, double ty, bool backward,
                     bool print = false, bool settle = true) {
    const double dt = 0.01, tau = 0.09, turnDisableLength = 6.0;
    SimplePID drive = {0.5, 9.0, 12.0}, heading = {0.1, 1.506, 12.0};
    double wall = 0;
    double lastV = 0;
    const double settleWithin = 12.0, settleV = 2.0;

    // classic moveTo's two stages, which moveTo.cpp now copies: FAR chases the
    // point, CLOSE freezes the heading and drives the remaining distance straight.
    bool   close = false;
    double heldHeading = 0, straightDist = 0, trackX = 0, trackY = 0;

    while (wall < 12.0 && hypot(tx - bot.x, ty - bot.y) > 1.0) {
        MoveToError e = ramsete.moveToError(tx, ty, bot.x, bot.y, bot.h, backward);

        double he = 0, le = 0;
        if (!close) {
            he = e.heading;
            le = e.distance;
            if (fabs(e.distance) < turnDisableLength) {
                close = true;
                heldHeading = bot.h;
                straightDist = le;
                trackX = bot.x;
                trackY = bot.y;
            }
        }
        if (close) {
            // yInchesTracked's stand-in: distance rolled along the frozen heading.
            double hr = heldHeading * DEG;
            he = wrapDegrees(heldHeading - bot.h);
            le = straightDist - ((bot.x - trackX) * sin(hr) + (bot.y - trackY) * cos(hr));
        }

        double v_d   = drive.out(le, dt * 1000.0);
        double v     = cos(he * DEG) * v_d;
        double omega = heading.out(he, dt * 1000.0);
        if (!close) omega += ramsete.moveToTurn(e, v);

        double L = v + omega, R = v - omega;
        double m = std::max(fabs(L), fabs(R));
        if (m > 12.0) { L *= 12.0 / m; R *= 12.0 / m; }

        double vps  = ramsete.maxVel / 12.0;               // in/s per volt
        double cmdV = (L + R) / 2 * vps;
        double cmdW = (L - R) * vps / ramsete.trackwidth / DEG;
        bot.v += (cmdV - bot.v) * (dt / tau);
        bot.w += (cmdW - bot.w) * (dt / tau);
        bot.x += bot.v * sin(bot.h * DEG) * dt;
        bot.y += bot.v * cos(bot.h * DEG) * dt;
        bot.h += bot.w * dt;
        wall  += dt;

        if (settle && fabs(bot.v) < fabs(lastV) && fabs(le) < settleWithin && fabs(bot.v) < settleV) break;
        lastV = bot.v;
    }
    if (print) printf("        finished wall=%.2fs at (%.1f, %.1f) h=%.1f\n", wall, bot.x, bot.y, bot.h);
    return bot;
}

static void testMoveTo() {
    printf("\n[9] moveTo: point-chase geometry, sign composition, convergence\n");

    // util.cpp's TO_DEG carries a rounded 57.29577951 rather than 180/pi, so a
    // half-turn lands about 1e-8 degrees off 180. Harmless, but it puts a floor
    // under how exactly anything that went through a 180° flip can compare.
    const double TINY = 1e-6;

    // --- geometry, forward ---
    MoveToError ahead = ramsete.moveToError(0, 24, 0, 0, 0, false);
    check(fabs(ahead.heading) < TINY,       "dead ahead: heading is zero (deg)", ahead.heading);
    check(fabs(ahead.distance - 24) < TINY, "dead ahead: distance (in)", ahead.distance);
    check(fabs(ahead.crossTrack) < TINY,    "dead ahead: no cross-track (in)", ahead.crossTrack);

    MoveToError right = ramsete.moveToError(10, 24, 0, 0, 0, false);
    check(right.heading > 0,    "target to the right: heading positive (deg)", right.heading);
    check(right.crossTrack > 0, "target to the right: crossTrack positive (in)", right.crossTrack);
    check(fabs(right.crossTrack - right.distance * sin(right.heading * DEG)) < TINY,
          "crossTrack is distance*sin(heading) by construction (in)", right.crossTrack);

    // This is what makes it point-chase. Turn the robot to face the target and the
    // cross-track error goes to zero with the heading — the two are one state. A
    // path-follower's cross-track would be unchanged by rotating in place.
    MoveToError aimed = ramsete.moveToError(10, 24, 0, 0, right.heading, false);
    check(fabs(aimed.crossTrack) < TINY,
          "rotating to face the target zeroes crossTrack (in)", aimed.crossTrack);

    // --- backward: the frame flips to the tail ---
    MoveToError back = ramsete.moveToError(0, -24, 0, 0, 0, true);
    check(fabs(back.heading) < TINY,       "backward, target behind: heading zero (deg)", back.heading);
    check(fabs(back.distance + 24) < TINY, "backward: distance is negative (in)", back.distance);

    // --- the sign composition ---
    // crossTrack carries the SIGNED distance so that driving backward flips it AND
    // flips v_d, and the two cancel inside their product. If either flip is missed
    // the steering term inverts and the robot curves away in reverse. Four cases:
    // both directions of travel, both sides of the target.
    for (int b = 0; b < 2; b++) {
        bool  drivingBack = (b == 1);
        double ty = drivingBack ? -24.0 : 24.0;
        for (int s = 0; s < 2; s++) {
            double tx = (s == 0) ? 10.0 : -10.0;
            MoveToError e = ramsete.moveToError(tx, ty, 0, 0, 0, drivingBack);
            double v_d  = 0.5 * e.distance;                    // what drivePID gives
            double v    = cos(e.heading * DEG) * v_d;
            double term = ramsete.moveToTurn(e, v);
            char label[96];
            snprintf(label, sizeof label, "%s, target %s: steering term agrees with heading",
                     drivingBack ? "backward" : "forward ", (s == 0) ? "right" : "left ");
            check(term * e.heading > 0, label, term);
        }
    }

    // --- overshoot ---
    // A target that has ended up behind a forward-driving robot puts the heading
    // past 90°, the cosine goes negative, and the movement backs into it instead of
    // spinning around to face it.
    MoveToError past = ramsete.moveToError(0, -12, 0, 0, 0, false);
    check(fabs(past.heading) > 90.0, "overshot target sits past 90 deg (deg)", past.heading);
    check(cos(past.heading * DEG) < 0, "overshoot: the cosine reverses the drive", cos(past.heading * DEG));

    // --- closed loop ---
    Sim f = runMoveTo({0, 0, 0, 0, 0}, 24, 48, false, true);
    check(hypot(24 - f.x, 48 - f.y) < 2.0, "forward: arrives at the point (in)",
          hypot(24 - f.x, 48 - f.y));

    // Started facing 140° away from the target, so it has to swing most of the way
    // round before the cosine lets it move at all.
    Sim m = runMoveTo({0, 0, 140, 0, 0}, 24, 48, false, true);
    check(hypot(24 - m.x, 48 - m.y) < 2.0, "badly misaligned start: still arrives (in)",
          hypot(24 - m.x, 48 - m.y));

    // The one the sign composition exists for.
    Sim r = runMoveTo({0, 0, 180, 0, 0}, 24, 48, true, true);
    check(hypot(24 - r.x, 48 - r.y) < 2.0, "backward: arrives at the point (in)",
          hypot(24 - r.x, 48 - r.y));

    // Timeout only, no settle gate. A movement that needs settle to look correct is
    // relying on a false positive — it exits while passing through zero speed, not
    // because it arrived. Both directions must converge on their own.
    Sim nf = runMoveTo({0, 0, 0, 0, 0}, 24, 48, false, true, false);
    check(hypot(24 - nf.x, 48 - nf.y) < 2.0, "forward, settle OFF: still converges (in)",
          hypot(24 - nf.x, 48 - nf.y));
    Sim nb = runMoveTo({0, 0, 180, 0, 0}, 24, 48, true, true, false);
    check(hypot(24 - nb.x, 48 - nb.y) < 2.0, "backward, settle OFF: still converges (in)",
          hypot(24 - nb.x, 48 - nb.y));
    double approach = atan2(24 - r.x, 48 - r.y) / DEG;
    check(fabs(fabs(wrapDegrees(approach - r.h)) - 180) < 25.0,
          "backward: arrives tail-first (deg from 180)",
          fabs(wrapDegrees(approach - r.h)));
}

static void testBoomerangTo() {
    printf("\n[10] boomerangTo: the curve the movement drives\n");
    const double TINY = 1e-6;

    // Robot at (10, 20) facing +X, arriving at (40, 60) travelling +Y.
    std::vector<BezierSegment> path = boomerangToPath(10, 20, 90, 40, 60, 0, 12, 8);
    check(path.size() == 1, "no padding: one segment", double(path.size()));
    BezierSegment s = path[0];

    check(fabs(s.a.x - 10) < TINY and fabs(s.a.y - 20) < TINY, "starts on the robot", s.a.x);
    check(fabs(s.d.x - 40) < TINY and fabs(s.d.y - 60) < TINY, "ends on the target", s.d.x);
    check(fabs(bezierHeading(s, 0.0) - 90) < TINY, "leaves along startHeading (deg)", bezierHeading(s, 0.0));
    check(fabs(bezierHeading(s, 1.0) -  0) < TINY, "arrives along endHeading (deg)", bezierHeading(s, 1.0));
    check(fabs(hypot(s.b.x - s.a.x, s.b.y - s.a.y) - 12) < TINY, "handle 1 length (in)", hypot(s.b.x - s.a.x, s.b.y - s.a.y));
    check(fabs(hypot(s.d.x - s.c.x, s.d.y - s.c.y) -  8) < TINY, "handle 2 length (in)", hypot(s.d.x - s.c.x, s.d.y - s.c.y));

    // The end handle has to sit BEHIND the target along endHeading. Put it past
    // the target and the curve arrives going the wrong way down the same line,
    // which no other check here would notice.
    check(s.c.y < s.d.y, "end handle sits behind the target, not past it (in)", s.c.y);

    // Padding: the cubic stops `padding` inches short along endHeading, and the rest is a line.
    std::vector<BezierSegment> pad = boomerangToPath(10, 20, 90, 40, 60, 0, 12, 8, 10);
    check(pad.size() == 2, "padding: curve plus a straight tail", double(pad.size()));

    BezierSegment curve = pad[0];
    BezierSegment line  = pad[1];
    check(fabs(curve.d.x - 40) < TINY and fabs(curve.d.y - 50) < TINY,
          "curve ends on the padded point (in)", curve.d.y);
    check(fabs(hypot(curve.d.x - curve.c.x, curve.d.y - curve.c.y) - 8) < TINY,
          "handle 2 stays that long off the PADDED anchor (in)", hypot(curve.d.x - curve.c.x, curve.d.y - curve.c.y));
    check(fabs(bezierHeading(curve, 1.0) - 0) < TINY, "curve still arrives along endHeading (deg)", bezierHeading(curve, 1.0));

    check(fabs(line.a.x - curve.d.x) < TINY and fabs(line.a.y - curve.d.y) < TINY, "tail starts where the curve ended (in)", line.a.y);
    check(fabs(line.d.x - 40) < TINY and fabs(line.d.y - 60) < TINY, "tail ends on the target (in)", line.d.y);

    // Straight means straight the whole way, not just at the ends: a bezier can leave and arrive on
    // heading and still bow out in between, and only the curvature at midspan would notice.
    double bow = 0;
    for (int i = 0; i <= 10; i++) {
        double t = i / 10.0;
        bow = std::max(bow, fabs(bezierCurvature(line, t)));
        bow = std::max(bow, fabs(wrapDegrees(bezierHeading(line, t) - 0)));
        bow = std::max(bow, fabs(bezierAt(line, t).x - 40));
    }
    check(bow < 1e-9, "tail is dead straight along endHeading (worst deviation)", bow);

    // Tangent at the joint, so the bake reads it as a joint and not a cusp it has to stop for.
    check(fabs(wrapDegrees(bezierHeading(curve, 1.0) - bezierHeading(line, 0.0))) < TINY,
          "curve and tail meet tangent (deg)", wrapDegrees(bezierHeading(curve, 1.0) - bezierHeading(line, 0.0)));

    // The joint is only a cusp if the bake stops for it. Read the speed at the row closest to the
    // padded point rather than the slowest row anywhere — the slowest row is the final brake.
    Trajectory padRows = createTrajectory(pad, {});
    double atJoint = 0;
    double nearest = 1e9;
    for (size_t i = 0; i < padRows.size(); i++) {
        double d = hypot(padRows[i].x - 40, padRows[i].y - 50);
        if (d < nearest) {
            nearest = d;
            atJoint = fabs(padRows[i].v);
        }
    }
    check(atJoint > 1.0, "padded path does not stop at the joint (in/s)", atJoint);
    check(fabs(padRows.back().x - 40) < 0.5 and fabs(padRows.back().y - 60) < 0.5,
          "padded schedule still ends on the target (in)", padRows.back().y);

    // Backward: the caller hands in heading + 180, so the curve leaves out the tail.
    BezierSegment b = boomerangToPath(10, 20, 90 + 180, 40, 60, 0, 12, 8)[0];
    check(fabs(wrapDegrees(bezierHeading(b, 0.0) - (-90))) < TINY,
          "backward: curve leaves along the tail (deg)", bezierHeading(b, 0.0));
    check(fabs(bezierHeading(b, 1.0) - 0) < TINY,
          "backward: endHeading is NOT flipped (deg)", bezierHeading(b, 1.0));

    // And it still bakes: a reversed segment gives negative-signed rows.
    BezierSegment rev = b;
    rev.reversed = true;
    Trajectory rows = createTrajectory({rev}, {});
    check(rows.size() > 2, "bakes into a schedule (rows)", double(rows.size()));
    check(std::signbit(rows[rows.size() / 2].v), "reversed rows carry negative v", rows[rows.size() / 2].v);
}

// Speed and remaining arc at a clock time, for the handoff checks.
static void atTime(const Trajectory& t, double when, double& speed, double& arcLeft) {
    std::size_t row = 0;
    for (std::size_t i = 0; i < t.size(); i++) {
        if (t[i].t <= when) row = i;
    }
    speed   = fabs(t[row].v);
    arcLeft = 0;
    for (std::size_t i = row + 1; i < t.size(); i++) {
        arcLeft += hypot(t[i].x - t[i - 1].x, t[i].y - t[i - 1].y);
    }
}

static void testTailHandoff() {
    printf("\n[11] straight tail: where the follower takes over\n");

    check(trajectoryTailStart(createTrajectory(boomerangToPath(10, 20, 90, 40, 60, 0, 12, 8), {}), 0) < 0,
          "no tail asked for: no handoff", 0);

    // The ordinary case: 10 in of padding on a move with room to run.
    Trajectory roomy = createTrajectory(boomerangToPath(0, 0, 0, 0, 60, 0, 12, 8, 10), {});
    double when = trajectoryTailStart(roomy, 10);
    double v, left;
    atTime(roomy, when, v, left);
    check(when > 0 and when < trajectoryDuration(roomy), "roomy: hands off mid-schedule (s)", when);
    check(fabs(left - 10) < 1.0, "roomy: hands off one padding from the end (in)", left);
    check(v > 1.0, "roomy: schedule is still moving at the handoff (in/s)", v);

    // The reason the arc is walked along the rows: this approach swings past the target and comes
    // back, so it is euclidean-close to the finish about 30 in of path before it is arc-close.
    Trajectory curl = createTrajectory(boomerangToPath(0, 0, 0, 0, 30, 180, 20, 8, 10), {});
    double curlWhen = trajectoryTailStart(curl, 10);
    atTime(curl, curlWhen, v, left);
    check(fabs(left - 10) < 1.5, "curl-around: handoff is arc-close, not fly-by close (in)", left);
    check(v > 1.0, "curl-around: still moving at the handoff (in/s)", v);

    // Padding as long as the whole move. Walking the arc alone would land on row zero, at rest;
    // the peak clamp pushes it onto the braking side instead.
    Trajectory tight = createTrajectory(boomerangToPath(0, 0, 0, 0, 10, 0, 12, 8, 10), {});
    double tightWhen = trajectoryTailStart(tight, 10);
    atTime(tight, tightWhen, v, left);
    double peak = 0;
    for (std::size_t i = 0; i < tight.size(); i++) peak = std::max(peak, fabs(tight[i].v));
    check(tightWhen > 0, "tail swallows the path: handoff is not row zero (s)", tightWhen);
    check(v > 1.0, "tail swallows the path: not at rest when it switches (in/s)", v);
    check(fabs(v - peak) < 1e-6, "tail swallows the path: switches at the profile peak (in/s)", v);
    check(left > 0.5, "tail swallows the path: still has path left to drive (in)", left);

    // Whatever the geometry, the switch never lands where the schedule has stopped.
    double pads[] = {1, 2, 5, 10, 16, 24};
    double reaches[] = {60, 36, 24, 16, 12, 10};
    double worst = 1e9;
    for (double reach : reaches) {
        for (double pad : pads) {
            Trajectory t = createTrajectory(boomerangToPath(0, 0, 0, 0, reach, 0, 12, 8, pad), {});
            double w = trajectoryTailStart(t, pad);
            if (w < 0) continue;
            atTime(t, w, v, left);
            worst = std::min(worst, v);
        }
    }
    check(worst > 1.0, "36 reach/padding pairs: slowest handoff speed (in/s)", worst);
}

int main() {
    printf("LucasLib ramsete — host checks\n");
    configureRamsete();
    testProfile();
    testFeasibility();
    testCusp();
    testLaw();
    testGainAndEdges();
    testPin();
    testMoveTo();
    testBoomerangTo();
    testTailHandoff();
    printf("\n%s (%d failures)\n", failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED", failures);
    return failures ? 1 : 0;
}
