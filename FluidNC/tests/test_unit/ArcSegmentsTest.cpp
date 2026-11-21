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

namespace {

const float kEps    = 5e-7F;  // ARC_ANGULAR_TRAVEL_EPSILON
const float kTwoPi  = 2 * float(M_PI);
const float kAngTol = 1e-5F;

// The former mc_arc() computation of the raw start-to-end angle, which went through the
// absolute center coordinate.  Kept here only to prove the reproducer below exercises it.
float old_raw_angle(const float start[2], const float end[2], const float offset[2]) {
    const float center[2] = { start[0] + offset[0], start[1] + offset[1] };
    const float rs[2]     = { -offset[0], -offset[1] };
    const float re[2]     = { end[0] - center[0], end[1] - center[1] };
    return atan2f(rs[0] * re[1] - rs[1] * re[0], rs[0] * re[0] + rs[1] * re[1]);
}

}  // namespace

// User report: small full circle far from the origin drew nothing, Z just went down and up.
//   G0 X105.0821 Y124.8697
//   G3 X105.0821 Y124.8697 I-0.0510 J0.6379
TEST(ArcAngularTravel, SmallFullCircleFarFromOrigin) {
    const float p[2]   = { 105.0821F, 124.8697F };
    const float off[2] = { -0.0510F, 0.6379F };

    // The old computation's rounding error exceeds the full-circle epsilon
    EXPECT_GT(fabsf(old_raw_angle(p, p, off)), kEps);

    EXPECT_NEAR(arc_angular_travel(p, p, off, false, 0, kEps), kTwoPi, kAngTol);   // G3
    EXPECT_NEAR(arc_angular_travel(p, p, off, true, 0, kEps), -kTwoPi, kAngTol);   // G2
}

// Property: start == end is always a full circle in the commanded direction, for any
// position, radius and center direction.
TEST(ArcAngularTravel, FullCircleRandom) {
    std::mt19937                          g(2);
    std::uniform_real_distribution<float> U(0.0F, 1.0F);
    for (int i = 0; i < 200000; i++) {
        float p[2]   = { 2000.0F * U(g) - 1000.0F, 2000.0F * U(g) - 1000.0F };  // -1000 .. 1000 mm
        float R      = 0.01F * powf(10.0F, 4.0F * U(g));                        // 0.01 .. 100 mm
        float a      = kTwoPi * U(g);
        float off[2] = { R * cosf(a), R * sinf(a) };
        bool  cw     = U(g) < 0.5F;
        float t      = arc_angular_travel(p, p, off, cw, 0, kEps);
        ASSERT_NEAR(t, cw ? -kTwoPi : kTwoPi, kAngTol) << "p=" << p[0] << "," << p[1] << " off=" << off[0] << "," << off[1];
    }
}

// Partial arcs keep their sign and size; a short arc is not mistaken for a full circle
TEST(ArcAngularTravel, PartialArcs) {
    const float s[2]   = { 110.0F, 100.0F };
    const float off[2] = { -10.0F, 0.0F };  // center (100, 100)
    const float q[2]   = { 100.0F, 110.0F };  // 90 degrees CCW from s
    EXPECT_NEAR(arc_angular_travel(s, q, off, false, 0, kEps), float(M_PI_2), kAngTol);       // G3 short way
    EXPECT_NEAR(arc_angular_travel(s, q, off, true, 0, kEps), -3 * float(M_PI_2), kAngTol);   // G2 long way

    const float tiny[2] = { 100.0F + 10.0F * cosf(1e-3F), 100.0F + 10.0F * sinf(1e-3F) };
    EXPECT_NEAR(arc_angular_travel(s, tiny, off, false, 0, kEps), 1e-3F, 1e-5F);
}

// P word: Pn adds n-1 extra turns; P0 and P1 are the plain arc
TEST(ArcAngularTravel, Rotations) {
    const float p[2]   = { 105.0821F, 124.8697F };
    const float off[2] = { -0.0510F, 0.6379F };
    EXPECT_NEAR(arc_angular_travel(p, p, off, false, 1, kEps), kTwoPi, kAngTol);
    EXPECT_NEAR(arc_angular_travel(p, p, off, false, 3, kEps), 3 * kTwoPi, 3 * kAngTol);
    EXPECT_NEAR(arc_angular_travel(p, p, off, true, 2, kEps), -2 * kTwoPi, 2 * kAngTol);
}
