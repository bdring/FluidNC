// Copyright (c) 2026 -  FluidNC Contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Arc tessellation math used by mc_arc(), kept free of planner dependencies
// so that it can be unit tested on the host.

#pragma once

#include <cmath>
#include <cstdint>

// Number of straight chords needed to approximate an arc of the given radius
// and angular travel so that no chord deviates from the arc by more than
// `tolerance`.  The divisor is half of the longest chord whose sagitta equals
// the tolerance, so the quotient is the (fractional) number of such chords.
// It must be rounded UP; rounding down emits chords longer than the tolerance
// allows (by up to 4x).  Always returns at least 1 for a non-degenerate arc.
inline uint16_t arc_segment_count(float radius, float angular_travel, float tolerance) {
    if (!(tolerance < 2 * radius)) {
        // A chord with sagitta `tolerance` would be wider than the circle, so one chord suffices.
        // Also keeps the division below away from inf/NaN.
        return 1;
    }
    float segments = ceilf(fabsf(0.5F * angular_travel * radius) / sqrtf(tolerance * (2 * radius - tolerance)));
    if (!(segments >= 1.0F)) {  // zero sweep, or NaN from a NaN input
        return 1;
    }
    return segments > 65535.0F ? uint16_t(65535) : uint16_t(segments);
}

// Signed angular travel (radians, positive = CCW) of an arc in its plane, from
// start to end around center = start + offset, including the extra turns that
// G2/G3 P<rotations> asks for.  A start-to-end angle within `epsilon` of zero in
// the wrong direction means a full circle.
//
// The center-to-end vector is computed as (end - start) - offset, not
// end - center.  Going through center adds and subtracts the large absolute
// coordinates, and the rounding of those (~7.6e-6 mm near 100 mm) divided by a
// small radius can exceed epsilon, turning a full circle into a null move.
// This way a full circle (end == start) gives exactly the start vector.
inline float arc_angular_travel(const float start[2], const float end[2], const float offset[2], bool is_clockwise_arc, uint32_t rotations, float epsilon) {
    const float rs[2] = { -offset[0], -offset[1] };                                        // center to start
    const float re[2] = { (end[0] - start[0]) - offset[0], (end[1] - start[1]) - offset[1] };  // center to end

    float angular_travel = atan2f(rs[0] * re[1] - rs[1] * re[0], rs[0] * re[0] + rs[1] * re[1]);
    // See https://linuxcnc.org/docs/2.6/html/gcode/gcode.html#sec:G2-G3-Arc
    // The P word specifies the number of extra rotations.  Missing P, P0 or P1
    // is just the programmed arc.  Pn adds n-1 rotations
    const float extra = rotations > 1 ? (rotations - 1) * 2 * float(M_PI) : 0.0F;
    if (is_clockwise_arc) {
        if (angular_travel >= -epsilon) {
            angular_travel -= 2 * float(M_PI);
        }
        angular_travel -= extra;
    } else {
        if (angular_travel <= epsilon) {
            angular_travel += 2 * float(M_PI);
        }
        angular_travel += extra;
    }
    return angular_travel;
}

// Per-segment rotation for stepping the radius vector around the arc.  Exact
// cos/sin, computed once per arc: the former third-order approximation lets the
// radius drift by a large fraction of the tolerance on small arcs, where
// theta_per_segment is large.
inline void arc_segment_rotation(float theta_per_segment, float& cos_T, float& sin_T) {
    cos_T = cosf(theta_per_segment);
    sin_T = sinf(theta_per_segment);
}
