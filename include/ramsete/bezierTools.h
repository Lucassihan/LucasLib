#pragma once

#include "misc/util.h"   // Pose, Vector2

// bezierTools — the spline layer. Position, tangent and curvature all fall out of the same four control points, so heading and curvature are exact.
// Frame: field inches, headings in degrees, 0 = +Y, clockwise positive.

// One cubic segment: endpoints a and d, handles b and c. Only their x and y matter — a control point has no facing.
struct BezierSegment {
    Pose   a;
    Pose   b;
    Pose   c;
    Pose   d;

    bool   reversed = false;   // drive this piece backward (heading flips 180°, v goes negative)
    bool   stopEnd  = false;   // force a full stop at this segment's end
    double maxVel   = -1;      // in/s ceiling for this segment   (-1 = profile default)
    double maxAccel = -1;      // in/s² budget for this segment   (-1 = profile default)
};

// Where the curve is at t. The returned Pose's heading field is unused — bezierHeading answers that.
Pose bezierAt(const BezierSegment& s, double t);

Vector2 bezierD(const BezierSegment& s, double t);
Vector2 bezierDD(const BezierSegment& s, double t);

// Tangent heading in degrees. atan2(p'x, p'y) IS the LucasLib convention, so no conversion is needed afterwards.
double bezierHeading(const BezierSegment& s, double t);

// Signed curvature in rad/in, CW+. The cross product is (p'y·p''x − p'x·p''y), mirroring the textbook math-frame order because LucasLib's heading runs the other way.
double bezierCurvature(const BezierSegment& s, double t);

// Arc length by sampling; no closed form exists for a cubic.
double bezierLength(const BezierSegment& s, int samples = 50);
