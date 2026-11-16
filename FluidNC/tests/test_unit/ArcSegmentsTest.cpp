// Test suite for the arc tessellation math used by mc_arc()
#include <gtest/gtest.h>
#include "ArcSegments.h"
#include <cmath>
#include <random>

namespace {

// Sagitta of one chord when an arc of `sweep` radians is split into n chords
// (computed in double: in float, 1-cos(x) for tiny x on a large radius is mostly rounding noise)
double sagitta(float radius, float sweep, unsigned n) {
    return double(radius) * (1.0 - cos(double(sweep) / (2.0 * n)));
}

}  // namespace

// Reproducer from bdring/FluidNC#1959: R = 0.15 mm, tolerance 0.002 mm.
// floorf gave 1 chord (sagitta 0.0077 mm, 3.85x the tolerance).
TEST(ArcSegments, Issue1959Reproducer) {
    const float R = 0.150F, sweep = 0.64350111F, tol = 0.002F;
    unsigned    n = arc_segment_count(R, sweep, tol);
    EXPECT_GE(n, 2u);
    EXPECT_LE(sagitta(R, sweep, n), tol);
}

// Property: the chord sagitta never exceeds the tolerance, for any radius,
// sweep and tolerance in the supported range.
TEST(ArcSegments, SagittaWithinToleranceRandom) {
    std::mt19937                          g(1);
    std::uniform_real_distribution<float> U(0.0F, 1.0F);
    for (int i = 0; i < 200000; i++) {
        float R     = 0.01F * powf(10.0F, 5.0F * U(g));   // 0.01 .. 1000 mm
        float tol   = 0.001F * powf(10.0F, 3.0F * U(g));  // 0.001 .. 1.0 mm
        float sweep = 0.01F + 6.28F * U(g);
        if (tol >= 2 * R) {
            continue;
        }
        unsigned n = arc_segment_count(R, sweep, tol);
        ASSERT_GE(n, 1u);
        // One part in 1e4 of slack for float rounding of the inputs
        ASSERT_LE(sagitta(R, sweep, n), double(tol) * 1.0001) << "R=" << R << " sweep=" << sweep << " tol=" << tol << " n=" << n;
    }
}

// Degenerate inputs must not yield 0 chords or garbage
TEST(ArcSegments, DegenerateInputs) {
    EXPECT_EQ(arc_segment_count(10.0F, 0.0F, 0.002F), 1u);      // zero sweep
    EXPECT_EQ(arc_segment_count(0.0005F, 1.0F, 0.002F), 1u);    // tolerance > 2R
    EXPECT_EQ(arc_segment_count(0.001F, 1.0F, 0.002F), 1u);     // tolerance == 2R: no inf -> 65535
    EXPECT_EQ(arc_segment_count(1e6F, 6.28F, 0.001F), 65535u);  // clamp, no uint16 overflow
}

// Exact rotation matches the true angle, unlike the old third-order form
TEST(ArcSegments, RotationIsExact) {
    for (float th : { 0.01F, 0.1F, 0.5F, 1.0F, 2.0F }) {
        float c, s;
        arc_segment_rotation(th, c, s);
        EXPECT_NEAR(c * c + s * s, 1.0F, 1e-6F);
        EXPECT_NEAR(atan2f(s, c), th, 1e-6F);
    }
}
