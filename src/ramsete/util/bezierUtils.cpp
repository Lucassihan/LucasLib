// bezierUtils.cpp — cubic bezier evaluation. Pure math: no vex.h, no globals, so the
// whole spline layer can be compiled and tested off the brain.

#include "ramsete/util/bezierUtils.h"
#include "misc/util.h"

#include <cmath>

Pose bezierAt(const BezierSegment& s, double t) {
    double u  = 1 - t;
    double w0 = u * u * u;
    double w1 = 3 * u * u * t;
    double w2 = 3 * u * t * t;
    double w3 = t * t * t;

    return {w0 * s.a.x + w1 * s.b.x + w2 * s.c.x + w3 * s.d.x,
            w0 * s.a.y + w1 * s.b.y + w2 * s.c.y + w3 * s.d.y};
}

Vector2 bezierD(const BezierSegment& s, double t) {
    double u  = 1 - t;
    double w0 = 3 * u * u;
    double w1 = 6 * u * t;
    double w2 = 3 * t * t;

    return {w0 * (s.b.x - s.a.x) + w1 * (s.c.x - s.b.x) + w2 * (s.d.x - s.c.x),
            w0 * (s.b.y - s.a.y) + w1 * (s.c.y - s.b.y) + w2 * (s.d.y - s.c.y)};
}

Vector2 bezierDD(const BezierSegment& s, double t) {
    double u = 1 - t;

    return {6 * u * (s.c.x - 2 * s.b.x + s.a.x) + 6 * t * (s.d.x - 2 * s.c.x + s.b.x),
            6 * u * (s.c.y - 2 * s.b.y + s.a.y) + 6 * t * (s.d.y - 2 * s.c.y + s.b.y)};
}

double bezierHeading(const BezierSegment& s, double t) {
    Vector2 d = bezierD(s, t);

    // A degenerate handle (both control points on the anchor) can zero the
    // derivative at an endpoint; nudge inward rather than return atan2(0, 0).
    if (fabs(d.x) < 1e-9 and fabs(d.y) < 1e-9) {
        d = bezierD(s, t < 0.5 ? t + 1e-4 : t - 1e-4);
    }
    return TO_DEG(atan2(d.x, d.y));
}

double bezierCurvature(const BezierSegment& s, double t) {
    Vector2 d  = bezierD(s, t);
    Vector2 dd = bezierDD(s, t);

    double speed = hypot(d.x, d.y);
    if (speed < 1e-9) {
        return 0;
    }
    return (d.y * dd.x - d.x * dd.y) / (speed * speed * speed);
}

double bezierLength(const BezierSegment& s, int samples) {
    if (samples < 1) {
        samples = 1;
    }
    double length = 0;
    Pose   last   = bezierAt(s, 0);

    for (int i = 1; i <= samples; i++) {
        Pose now = bezierAt(s, double(i) / samples);
        length += hypot(now.x - last.x, now.y - last.y);
        last = now;
    }
    return length;
}
