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

// Per-segment rotation for stepping the radius vector around the arc.  Exact
// cos/sin, computed once per arc: the former third-order approximation lets the
// radius drift by a large fraction of the tolerance on small arcs, where
// theta_per_segment is large.
inline void arc_segment_rotation(float theta_per_segment, float& cos_T, float& sin_T) {
    cos_T = cosf(theta_per_segment);
    sin_T = sinf(theta_per_segment);
}
