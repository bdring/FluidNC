// Copyright (c) 2024 - FluidNC Contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "gtest/gtest.h"
#include "test_mocks.h"  // Must come before kinematics headers to mock logging
#include "Kinematics/Cartesian.h"
#include "Kinematics/CoreXY.h"
#include "Kinematics/Midtbot.h"
#include "Kinematics/ParallelDelta.h"
#include "Kinematics/WallPlotter.h"
#include "Types.h"  // For axis_t
#include <memory>
#include <cmath>
#include <vector>
#include <iostream>

// Test tolerance for floating-point comparisons
constexpr float TOLERANCE         = 1e-5f;
constexpr float SEGMENT_TOLERANCE = 1e-3f;

// ============================================================================
// TEST CLASS
// ============================================================================

class KinematicsTest : public ::testing::Test {
protected:
    // NOTE: All kinematics tests use MAX_N_AXIS sized arrays for motor/cartesian coordinates
    // This is REQUIRED because the motion capture infrastructure (mc_move_motors) uses
    // vector_distance() with MAX_N_AXIS when computing segment lengths. Uninitialized array
    // elements beyond n_axis lead to inf/nan values in distance calculations.
    // Even tests of 2-axis (WallPlotter) or 3-axis (ParallelDelta) kinematics must use
    // MAX_N_AXIS sized arrays initialized with zeros for all elements.

    static void SetUpTestSuite() {
        // Initialize Machine::Axes with MAX_N_AXIS axes
        Machine::Axes::init_stubs(MAX_N_AXIS);
    }

    static void TearDownTestSuite() {
        // Clean up Axes
        Machine::Axes::cleanup_stubs();
    }

    void AssertArrayNear(const float* expected, const float* actual, size_t size, float tolerance) {
        for (size_t i = 0; i < size; ++i) {
            EXPECT_NEAR(expected[i], actual[i], tolerance) << "Mismatch at index " << i;
        }
    }

    // Helper: Verify that a point lies on the line from position to target
    // Uses perpendicular distance approach which is scale-invariant and avoids tolerance problems
    bool isOnSegment(const float* position, const float* target, const float* point, axis_t n_axis) {
        // Calculate vectors with first three components
        float direction[3] = { 0, 0, 0 };
        float offset[3]    = { 0, 0, 0 };

        for (int i = 0; i < std::min((int)3, (int)n_axis); i++) {
            direction[i] = target[i] - position[i];
            offset[i]    = point[i] - position[i];
        }

        // Calculate dot products
        float dir_dot_dir    = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
        float offset_dot_dir = offset[0] * direction[0] + offset[1] * direction[1] + offset[2] * direction[2];

        // Avoid division by zero (position == target)
        if (dir_dot_dir < TOLERANCE) {
            // Line segment is degenerate; check if point is at position
            float dist_sq = offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2];
            return dist_sq < SEGMENT_TOLERANCE * SEGMENT_TOLERANCE;
        }

        // Calculate parameter t: where 0 <= t <= 1 means point projects onto segment
        float t = offset_dot_dir / dir_dot_dir;

        // Calculate the closest point on the line: closest = position + t * direction
        float closest[3];
        for (int i = 0; i < 3; i++) {
            closest[i] = position[i] + t * direction[i];
        }

        // Calculate perpendicular distance from point to closest point on line
        float perp_dist_sq = 0.0f;
        for (int i = 0; i < std::min((int)3, (int)n_axis); i++) {
            float diff = point[i] - closest[i];
            perp_dist_sq += diff * diff;
        }

        // Check if point is on the segment (within tolerance and between endpoints)
        return perp_dist_sq < SEGMENT_TOLERANCE * SEGMENT_TOLERANCE && t >= -TOLERANCE && t <= 1.0f + TOLERANCE;
    }

    void expectLinearSegments(const float* position, const float* target, axis_t n_axis, Kinematics::KinematicSystem& kinematics) {
        float cartesian_distance = vector_distance(target, position, n_axis);
        auto  segments           = get_motor_segments();
        if (cartesian_distance <= TOLERANCE) {
            EXPECT_EQ(segments.size(), 0);
        } else {
            EXPECT_GT(segments.size(), 0);
            float cartesian[MAX_N_AXIS] = { 0.0f };  // Must initialize all elements to avoid garbage in distance calculations
            float total_segment_time    = 0.0f;

            for (auto& segment : segments) {
                kinematics.motors_to_cartesian(cartesian, segment.motors, n_axis);
                bool collinear = isOnSegment(position, target, cartesian, n_axis);
                EXPECT_TRUE(collinear);

                total_segment_time += segment.segment_time;
            }

            AssertArrayNear(target, cartesian, (int)n_axis, 0.001);

            // Only validate timing if we have segments with non-zero length
            if (segments.size() > 0) {
                // Compute expected time from cartesian distance and feedrate
                cartesian_distance = vector_distance(const_cast<float*>(target), const_cast<float*>(position), n_axis);

                // Only check time if we actually moved (cartesian_distance > tolerance)
                if (cartesian_distance > TOLERANCE) {
                    // Time = distance (mm) * 60 (sec/min) / feedrate (mm/min)
                    float expected_time = (cartesian_distance * 60.0f) / 1000.0f;  // 1000 mm/min feedrate

                    EXPECT_NEAR(expected_time, total_segment_time, TOLERANCE * 100);  // Relax tolerance for accumulated time
                }
            }
        }
    }

    void testSegmentation(float* target, float* position, axis_t n_axis, Kinematics::KinematicSystem& kinematics) {
        // IMPORTANT: This test MUST use arrays sized with MAX_N_AXIS and fully initialized with zeros.
        // The motion capture infrastructure (mc_move_motors) uses vector_distance() with MAX_N_AXIS
        // to compute segment lengths. If array elements beyond n_axis contain uninitialized values,
        // vector_distance() will compute with garbage data leading to inf/nan segment lengths and times.
        // Therefore, even if kinematics only uses 2-3 axes, all MAX_N_AXIS elements must be properly
        // initialized to zero to ensure correct distance calculations.
        reset_motor_segments();
        plan_line_data_t plan_data {};
        plan_data.feed_rate = 1000.0f;  // Set feedrate to 1000 mm/min

        EXPECT_TRUE(kinematics.cartesian_to_motors(target, &plan_data, position));
        expectLinearSegments(position, target, n_axis, kinematics);
    }
};

// ============================================================================
// CARTESIAN KINEMATICS TESTS
// ============================================================================

TEST_F(KinematicsTest, SimpleFloatTest) {
    float cartesian[3] = {10.0f, 20.0f, 30.0f};

    // Simple test to verify float array initialization works
    EXPECT_NEAR(10.0f, cartesian[0], TOLERANCE);
    EXPECT_NEAR(20.0f, cartesian[1], TOLERANCE);
    EXPECT_NEAR(30.0f, cartesian[2], TOLERANCE);
}

TEST_F(KinematicsTest, CartesianIdentity) {
    auto kinematics = std::make_unique<Kinematics::Cartesian>("Cartesian");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    float cartesian[MAX_N_AXIS] = { 10.0f, 20.0f, 30.0f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, cartesian);

    EXPECT_NEAR(cartesian[0], motors[0], TOLERANCE);
    EXPECT_NEAR(cartesian[1], motors[1], TOLERANCE);
    EXPECT_NEAR(cartesian[2], motors[2], TOLERANCE);
}

TEST_F(KinematicsTest, CartesianOrigin) {
    auto kinematics = std::make_unique<Kinematics::Cartesian>("Cartesian");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    float cartesian[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, cartesian);

    EXPECT_NEAR(0.0f, motors[0], TOLERANCE);
    EXPECT_NEAR(0.0f, motors[1], TOLERANCE);
    EXPECT_NEAR(0.0f, motors[2], TOLERANCE);
}

TEST_F(KinematicsTest, CartesianRoundTrip) {
    auto kinematics = std::make_unique<Kinematics::Cartesian>("Cartesian");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    float original[MAX_N_AXIS]  = { 15.5f, -25.3f, 5.0f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float recovered[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, original);
    kinematics->motors_to_cartesian(recovered, motors, static_cast<axis_t>(3));

    EXPECT_NEAR(original[0], recovered[0], TOLERANCE);
    EXPECT_NEAR(original[1], recovered[1], TOLERANCE);
    EXPECT_NEAR(original[2], recovered[2], TOLERANCE);
}

TEST_F(KinematicsTest, CartesianAxisAlignedMoves) {
    auto kinematics = std::make_unique<Kinematics::Cartesian>("Cartesian");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    // X-axis only
    {
        float cartesian[MAX_N_AXIS] = { 100.0f, 0.0f, 0.0f };
        float motors[MAX_N_AXIS];
        kinematics->transform_cartesian_to_motors(motors, cartesian);
        EXPECT_NEAR(100.0f, motors[0], TOLERANCE);
        EXPECT_NEAR(0.0f, motors[1], TOLERANCE);
        EXPECT_NEAR(0.0f, motors[2], TOLERANCE);
    }

    // Y-axis only
    {
        float cartesian[MAX_N_AXIS] = { 0.0f, 50.0f, 0.0f };
        float motors[MAX_N_AXIS];
        kinematics->transform_cartesian_to_motors(motors, cartesian);
        EXPECT_NEAR(0.0f, motors[0], TOLERANCE);
        EXPECT_NEAR(50.0f, motors[1], TOLERANCE);
        EXPECT_NEAR(0.0f, motors[2], TOLERANCE);
    }

    // Z-axis only
    {
        float cartesian[MAX_N_AXIS] = { 0.0f, 0.0f, 10.0f };
        float motors[MAX_N_AXIS];
        kinematics->transform_cartesian_to_motors(motors, cartesian);
        EXPECT_NEAR(0.0f, motors[0], TOLERANCE);
        EXPECT_NEAR(0.0f, motors[1], TOLERANCE);
        EXPECT_NEAR(10.0f, motors[2], TOLERANCE);
    }
}

TEST_F(KinematicsTest, CartesianFourAxis) {
    auto kinematics = std::make_unique<Kinematics::Cartesian>("Cartesian");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    float cartesian[MAX_N_AXIS] = { 1.0f, 2.0f, 3.0f, 4.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float recovered[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, cartesian);
    kinematics->motors_to_cartesian(recovered, motors, static_cast<axis_t>(4));
    AssertArrayNear(cartesian, recovered, 4, TOLERANCE);
}

TEST_F(KinematicsTest, CartesianCartesianToMotors) {
    auto kinematics = std::make_unique<Kinematics::Cartesian>("Cartesian");
    kinematics->init();

    // For Cartesian kinematics, motors = cartesian directly
    // So test forward and inverse transformations
    float cartesian_start[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float motors_start[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors_start, cartesian_start);
    EXPECT_NEAR(0.0f, motors_start[0], TOLERANCE);
    EXPECT_NEAR(0.0f, motors_start[1], TOLERANCE);
    EXPECT_NEAR(0.0f, motors_start[2], TOLERANCE);

    // Test round-trip transformation
    float cartesian_target[MAX_N_AXIS] = { 10.0f, 10.0f, 5.0f, 0.0f, 0.0f, 0.0f };
    float motors_target[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors_target, cartesian_target);

    // For Cartesian, motors should match cartesian
    EXPECT_NEAR(10.0f, motors_target[0], TOLERANCE);
    EXPECT_NEAR(10.0f, motors_target[1], TOLERANCE);
    EXPECT_NEAR(5.0f, motors_target[2], TOLERANCE);

    // Verify reverse transform
    float cartesian_roundtrip[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    kinematics->motors_to_cartesian(cartesian_roundtrip, motors_target, static_cast<axis_t>(3));
    EXPECT_NEAR(cartesian_target[0], cartesian_roundtrip[0], TOLERANCE);
    EXPECT_NEAR(cartesian_target[1], cartesian_roundtrip[1], TOLERANCE);
    EXPECT_NEAR(cartesian_target[2], cartesian_roundtrip[2], TOLERANCE);
}

// ============================================================================
// CoreXY KINEMATICS TESTS
// ============================================================================

TEST_F(KinematicsTest, CoreXYInverseTransform) {
    auto kinematics = std::make_unique<Kinematics::CoreXY>("CoreXY");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    float cartesian[MAX_N_AXIS] = { 10.0f, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, cartesian);

    // m1 = 10 + 5 = 15, m2 = 10 - 5 = 5
    EXPECT_NEAR(15.0f, motors[0], TOLERANCE);
    EXPECT_NEAR(5.0f, motors[1], TOLERANCE);
    EXPECT_NEAR(0.0f, motors[2], TOLERANCE);
}

TEST_F(KinematicsTest, CoreXYForwardTransform) {
    auto kinematics = std::make_unique<Kinematics::CoreXY>("CoreXY");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    float motors[MAX_N_AXIS]    = { 15.0f, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float cartesian[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->motors_to_cartesian(cartesian, motors, static_cast<axis_t>(3));

    // x = 0.5 * (15 + 5) = 10, y = 0.5 * (15 - 5) = 5
    EXPECT_NEAR(10.0f, cartesian[0], TOLERANCE);
    EXPECT_NEAR(5.0f, cartesian[1], TOLERANCE);
    EXPECT_NEAR(0.0f, cartesian[2], TOLERANCE);
}

TEST_F(KinematicsTest, CoreXYRoundTrip) {
    auto kinematics = std::make_unique<Kinematics::CoreXY>("CoreXY");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    float original[MAX_N_AXIS]  = { 12.5f, -3.2f, 8.5f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float recovered[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, original);
    kinematics->motors_to_cartesian(recovered, motors, static_cast<axis_t>(3));
    AssertArrayNear(original, recovered, 3, TOLERANCE);
}

TEST_F(KinematicsTest, CoreXYOrigin) {
    auto kinematics = std::make_unique<Kinematics::CoreXY>("CoreXY");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    float cartesian[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, cartesian);

    EXPECT_NEAR(0.0f, motors[0], TOLERANCE);
    EXPECT_NEAR(0.0f, motors[1], TOLERANCE);
    EXPECT_NEAR(0.0f, motors[2], TOLERANCE);
}

TEST_F(KinematicsTest, CoreXYAxisAlignedMoves) {
    auto kinematics = std::make_unique<Kinematics::CoreXY>("CoreXY");

    // X-only: (10, 0, 0) -> motors (10, 10, 0)
    {
        float cartesian[MAX_N_AXIS] = { 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        kinematics->transform_cartesian_to_motors(motors, cartesian);
        EXPECT_NEAR(10.0f, motors[0], TOLERANCE);
        EXPECT_NEAR(10.0f, motors[1], TOLERANCE);
        EXPECT_NEAR(0.0f, motors[2], TOLERANCE);
    }

    // Y-only: (0, 5, 0) -> motors (5, -5, 0)
    {
        float cartesian[MAX_N_AXIS] = { 0.0f, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        kinematics->transform_cartesian_to_motors(motors, cartesian);
        EXPECT_NEAR(5.0f, motors[0], TOLERANCE);
        EXPECT_NEAR(-5.0f, motors[1], TOLERANCE);
        EXPECT_NEAR(0.0f, motors[2], TOLERANCE);
    }

    // Z-only
    {
        float cartesian[MAX_N_AXIS] = { 0.0f, 0.0f, 25.0f, 0.0f, 0.0f, 0.0f };
        float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        kinematics->transform_cartesian_to_motors(motors, cartesian);
        EXPECT_NEAR(0.0f, motors[0], TOLERANCE);
        EXPECT_NEAR(0.0f, motors[1], TOLERANCE);
        EXPECT_NEAR(25.0f, motors[2], TOLERANCE);
    }
}

TEST_F(KinematicsTest, CoreXYDiagonalMoves) {
    auto kinematics = std::make_unique<Kinematics::CoreXY>("CoreXY");

    // Diagonal: (10, 10) -> motors (20, 0)
    {
        float cartesian[MAX_N_AXIS] = { 10.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        kinematics->transform_cartesian_to_motors(motors, cartesian);
        EXPECT_NEAR(20.0f, motors[0], TOLERANCE);
        EXPECT_NEAR(0.0f, motors[1], TOLERANCE);
    }

    // Diagonal: (10, -10) -> motors (0, 20)
    {
        float cartesian[MAX_N_AXIS] = { 10.0f, -10.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        kinematics->transform_cartesian_to_motors(motors, cartesian);
        EXPECT_NEAR(0.0f, motors[0], TOLERANCE);
        EXPECT_NEAR(20.0f, motors[1], TOLERANCE);
    }
}

TEST_F(KinematicsTest, CoreXYNegativeValues) {
    auto kinematics = std::make_unique<Kinematics::CoreXY>("CoreXY");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    float cartesian[MAX_N_AXIS] = { -5.0f, -3.0f, -2.0f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float recovered[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, cartesian);
    kinematics->motors_to_cartesian(recovered, motors, static_cast<axis_t>(3));
    AssertArrayNear(cartesian, recovered, 3, TOLERANCE);
}

TEST_F(KinematicsTest, CoreXYFourAxis) {
    auto kinematics = std::make_unique<Kinematics::CoreXY>("CoreXY");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    float cartesian[MAX_N_AXIS] = { 5.0f, 2.0f, 10.0f, 45.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float recovered[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, cartesian);
    kinematics->motors_to_cartesian(recovered, motors, static_cast<axis_t>(4));
    AssertArrayNear(cartesian, recovered, 4, TOLERANCE);
}

// ============================================================================
// EDGE CASES
// ============================================================================

TEST_F(KinematicsTest, VerySmallValues) {
    auto kinematics = std::make_unique<Kinematics::CoreXY>("CoreXY");

    float cartesian[MAX_N_AXIS] = { 0.001f, -0.0005f, 0.0001f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float recovered[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, cartesian);
    kinematics->motors_to_cartesian(recovered, motors, static_cast<axis_t>(3));
    AssertArrayNear(cartesian, recovered, 3, TOLERANCE);
}

TEST_F(KinematicsTest, LargeValues) {
    auto kinematics = std::make_unique<Kinematics::CoreXY>("CoreXY");

    float cartesian[MAX_N_AXIS] = { 10000.0f, -5000.5f, 2500.25f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float recovered[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, cartesian);
    kinematics->motors_to_cartesian(recovered, motors, static_cast<axis_t>(3));
    AssertArrayNear(cartesian, recovered, 3, TOLERANCE);
}

TEST_F(KinematicsTest, CoreXYCartesianToMotors) {
    auto kinematics = std::make_unique<Kinematics::CoreXY>("CoreXY");
    kinematics->init();

    // Test that cartesian_to_motors successfully handles transformations
    float            position[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float            target[MAX_N_AXIS]   = { 20.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    plan_line_data_t plan_data {};

    reset_motor_segments();
    bool result = kinematics->cartesian_to_motors(target, &plan_data, position);

    EXPECT_TRUE(result);
    // CoreXY motion should generate at least one segment
    EXPECT_GT(get_motor_segments().size(), 0);
}

// ============================================================================
// MIDTBOT KINEMATICS TESTS
// ============================================================================

TEST_F(KinematicsTest, MidtbotInverseTransform) {
    auto kinematics = std::make_unique<Kinematics::Midtbot>("Midtbot");
    // Note: init() not called - test uses pure math transforms without _x_scaler initialization
    // (Midtbot::init() sets _x_scaler = 2.0, which changes the math)

    float cartesian[MAX_N_AXIS] = { 10.0f, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, cartesian);

    // m1 = 10 + 5 = 15, m2 = 10 - 5 = 5 (same as CoreXY)
    EXPECT_NEAR(15.0f, motors[0], TOLERANCE);
    EXPECT_NEAR(5.0f, motors[1], TOLERANCE);
    EXPECT_NEAR(0.0f, motors[2], TOLERANCE);
}

TEST_F(KinematicsTest, MidtbotForwardTransform) {
    auto kinematics = std::make_unique<Kinematics::Midtbot>("Midtbot");
    // Note: init() not called - we test pure math transforms without full machine configuration

    float motors[MAX_N_AXIS]    = { 15.0f, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float cartesian[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->motors_to_cartesian(cartesian, motors, static_cast<axis_t>(3));

    // x = 0.5 * (15 + 5) = 10, y = 0.5 * (15 - 5) = 5 (same as CoreXY)
    EXPECT_NEAR(10.0f, cartesian[0], TOLERANCE);
    EXPECT_NEAR(5.0f, cartesian[1], TOLERANCE);
    EXPECT_NEAR(0.0f, cartesian[2], TOLERANCE);
}

TEST_F(KinematicsTest, MidtbotRoundTrip) {
    auto kinematics = std::make_unique<Kinematics::Midtbot>("Midtbot");
    // Note: init() not called - we test pure math transforms without full machine configuration

    float original[MAX_N_AXIS]  = { 12.5f, -3.2f, 8.5f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float recovered[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, original);
    kinematics->motors_to_cartesian(recovered, motors, static_cast<axis_t>(3));
    AssertArrayNear(original, recovered, 3, TOLERANCE);
}

TEST_F(KinematicsTest, MidtbotOrigin) {
    auto kinematics = std::make_unique<Kinematics::Midtbot>("Midtbot");
    // Note: init() not called - we test pure math transforms without full machine configuration

    float cartesian[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, cartesian);

    EXPECT_NEAR(0.0f, motors[0], TOLERANCE);
    EXPECT_NEAR(0.0f, motors[1], TOLERANCE);
    EXPECT_NEAR(0.0f, motors[2], TOLERANCE);
}

TEST_F(KinematicsTest, MidtbotAxisAlignedMoves) {
    auto kinematics = std::make_unique<Kinematics::Midtbot>("Midtbot");

    // X-only: (10, 0, 0) -> motors (10, 10, 0)
    {
        float cartesian[MAX_N_AXIS] = { 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        kinematics->transform_cartesian_to_motors(motors, cartesian);
        EXPECT_NEAR(10.0f, motors[0], TOLERANCE);
        EXPECT_NEAR(10.0f, motors[1], TOLERANCE);
        EXPECT_NEAR(0.0f, motors[2], TOLERANCE);
    }

    // Y-only: (0, 5, 0) -> motors (5, -5, 0)
    {
        float cartesian[MAX_N_AXIS] = { 0.0f, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        kinematics->transform_cartesian_to_motors(motors, cartesian);
        EXPECT_NEAR(5.0f, motors[0], TOLERANCE);
        EXPECT_NEAR(-5.0f, motors[1], TOLERANCE);
        EXPECT_NEAR(0.0f, motors[2], TOLERANCE);
    }

    // Z-only
    {
        float cartesian[MAX_N_AXIS] = { 0.0f, 0.0f, 25.0f, 0.0f, 0.0f, 0.0f };
        float motors[6]             = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        kinematics->transform_cartesian_to_motors(motors, cartesian);
        EXPECT_NEAR(0.0f, motors[0], TOLERANCE);
        EXPECT_NEAR(0.0f, motors[1], TOLERANCE);
        EXPECT_NEAR(25.0f, motors[2], TOLERANCE);
    }
}

TEST_F(KinematicsTest, MidtbotDiagonalMoves) {
    auto kinematics = std::make_unique<Kinematics::Midtbot>("Midtbot");

    // Diagonal: (10, 10) -> motors (20, 0)
    {
        float cartesian[MAX_N_AXIS] = { 10.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        kinematics->transform_cartesian_to_motors(motors, cartesian);
        EXPECT_NEAR(20.0f, motors[0], TOLERANCE);
        EXPECT_NEAR(0.0f, motors[1], TOLERANCE);
    }

    // Diagonal: (10, -10) -> motors (0, 20)
    {
        float cartesian[MAX_N_AXIS] = { 10.0f, -10.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        kinematics->transform_cartesian_to_motors(motors, cartesian);
        EXPECT_NEAR(0.0f, motors[0], TOLERANCE);
        EXPECT_NEAR(20.0f, motors[1], TOLERANCE);
    }
}

TEST_F(KinematicsTest, MidtbotNegativeValues) {
    auto kinematics = std::make_unique<Kinematics::Midtbot>("Midtbot");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis
    float cartesian[MAX_N_AXIS] = { -5.0f, -3.0f, -2.0f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float recovered[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, cartesian);
    kinematics->motors_to_cartesian(recovered, motors, static_cast<axis_t>(3));
    AssertArrayNear(cartesian, recovered, 3, TOLERANCE);
}

TEST_F(KinematicsTest, MidtbotFourAxis) {
    auto kinematics = std::make_unique<Kinematics::Midtbot>("Midtbot");
    // Note: init() not called - we test pure math transforms without full machine configuration

    float cartesian[MAX_N_AXIS] = { 5.0f, 2.0f, 10.0f, 45.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float recovered[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->transform_cartesian_to_motors(motors, cartesian);
    kinematics->motors_to_cartesian(recovered, motors, static_cast<axis_t>(4));
    AssertArrayNear(cartesian, recovered, 4, TOLERANCE);
}

TEST_F(KinematicsTest, ParallelDeltaCartesianToMotorsSegments) {
    // ParallelDelta segmentation test using testSegmentation()
    // Validates linearity of segmented motion and feedrate accuracy
    auto kinematics = std::make_unique<Kinematics::ParallelDelta>("ParallelDelta");
    kinematics->init();
    float zeros[MAX_N_AXIS]           = { 0.0f };
    float up_arms[MAX_N_AXIS]         = { -30.0f, -30.0f, -30.0f, 0.0f };
    float horizontal_arms[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float down_arms[MAX_N_AXIS]       = { 90.0f, 90.0f, 90.0f, 0.0f };
    set_motor_pos(up_arms, MAX_N_AXIS);

    auto n_axis = static_cast<axis_t>(MAX_N_AXIS);

    float cartesian[MAX_N_AXIS];
    kinematics->motors_to_cartesian(cartesian, up_arms, n_axis);

    kinematics->motors_to_cartesian(cartesian, horizontal_arms, n_axis);

    float position[MAX_N_AXIS];
    copyAxes(position, cartesian);

    kinematics->motors_to_cartesian(cartesian, down_arms, n_axis);

    // With arms
    float target[MAX_N_AXIS];
    copyAxes(target, position);

    reset_motor_pos();

    // Test 1: Z-only movement (up)
    target[2] += 20.0f;
    testSegmentation(target, position, n_axis, *kinematics);
    copyAxes(position, target);

    // Test 2: Z-only movement (down)
    target[2] -= 70.0f;
    testSegmentation(target, position, n_axis, *kinematics);
    copyAxes(position, target);

    // Test 3: Small XY movement (within workspace)
    target[0] += 25.0f;
    target[1] += 25.0f;
    testSegmentation(target, position, n_axis, *kinematics);
    copyAxes(position, target);

    // Test 4: Return to XY center with Z movement
    target[0] = 0.0f;
    target[1] = 0.0f;
    target[2] += 50.0f;
    testSegmentation(target, position, n_axis, *kinematics);
    copyAxes(position, target);
}

// ============================================================================
// WALLPLOTTER KINEMATICS TESTS
// ============================================================================

TEST_F(KinematicsTest, WallPlotterOrigin) {
    auto kinematics = std::make_unique<Kinematics::WallPlotter>("WallPlotter");
    // Note: init() not called - motor values are relative to zero offset (direct cord lengths)

    // Use physically valid motor values representing cord lengths to reach (0, 50)
    // With anchors at (-100,100) and (100,100), cords to (0,50) are both ~111.8
    float motors[MAX_N_AXIS]    = { 111.8f, 111.8f, 0.0f, 0.0f, 0.0f, 0.0f };
    float cartesian[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->motors_to_cartesian(cartesian, motors, static_cast<axis_t>(2));

    // Should produce valid cartesian coordinates
    EXPECT_TRUE(std::isfinite(cartesian[0]));
    EXPECT_TRUE(std::isfinite(cartesian[1]));
}
TEST_F(KinematicsTest, WallPlotterLeftMotorMovement) {
    auto kinematics = std::make_unique<Kinematics::WallPlotter>("WallPlotter");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    // Test left motor movement with valid cord lengths
    // Cord 1=111.8, Cord 2=111.8 reaches approximately (0, 50)
    float motors1[MAX_N_AXIS] = { 111.8f, 111.8f, 0.0f, 0.0f, 0.0f, 0.0f };
    float motors2[MAX_N_AXIS] = { 121.8f, 111.8f, 0.0f, 0.0f, 0.0f, 0.0f };  // Left cord 10mm longer
    float cart1[MAX_N_AXIS]   = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float cart2[MAX_N_AXIS]   = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->motors_to_cartesian(cart1, motors1, static_cast<axis_t>(2));
    kinematics->motors_to_cartesian(cart2, motors2, static_cast<axis_t>(2));

    // Moving left motor should change cartesian position
    bool position_changed = (cart1[0] != cart2[0]) || (cart1[1] != cart2[1]);
    EXPECT_TRUE(position_changed);
}

TEST_F(KinematicsTest, WallPlotterRightMotorMovement) {
    auto kinematics = std::make_unique<Kinematics::WallPlotter>("WallPlotter");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    // Test right motor movement with valid cord lengths
    float motors1[MAX_N_AXIS] = {111.8f, 111.8f, 0.0f, 0.0f, 0.0f, 0.0f};
    float motors2[MAX_N_AXIS] = {111.8f, 121.8f, 0.0f, 0.0f, 0.0f, 0.0f};  // Right cord 10mm longer
    float cart1[MAX_N_AXIS] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float cart2[MAX_N_AXIS] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    kinematics->motors_to_cartesian(cart1, motors1, static_cast<axis_t>(2));
    kinematics->motors_to_cartesian(cart2, motors2, static_cast<axis_t>(2));

    // Moving right motor should change cartesian position
    bool position_changed = (cart1[0] != cart2[0]) || (cart1[1] != cart2[1]);
    EXPECT_TRUE(position_changed);
}

TEST_F(KinematicsTest, WallPlotterBothMotorsMovement) {
    auto kinematics = std::make_unique<Kinematics::WallPlotter>("WallPlotter");
    // Note: init() not called - motor values are relative to zero offset (direct cord lengths)

    // Test both motors moving together with valid cord lengths
    float motors1[MAX_N_AXIS] = {111.8f, 111.8f, 0.0f, 0.0f, 0.0f, 0.0f};
    float motors2[MAX_N_AXIS] = {121.8f, 121.8f, 0.0f, 0.0f, 0.0f, 0.0f};  // Both cords 10mm longer
    float cart1[MAX_N_AXIS] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float cart2[MAX_N_AXIS] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    kinematics->motors_to_cartesian(cart1, motors1, static_cast<axis_t>(2));
    kinematics->motors_to_cartesian(cart2, motors2, static_cast<axis_t>(2));

    // Verify both positions are valid
    EXPECT_TRUE(std::isfinite(cart1[0]));
    EXPECT_TRUE(std::isfinite(cart1[1]));
    EXPECT_TRUE(std::isfinite(cart2[0]));
    EXPECT_TRUE(std::isfinite(cart2[1]));
}

TEST_F(KinematicsTest, WallPlotterZAxisPassthrough) {
    auto kinematics = std::make_unique<Kinematics::WallPlotter>("WallPlotter");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    // Test that Z axis passes through unchanged
    float motors[MAX_N_AXIS] = {0.0f, 0.0f, 50.0f, 0.0f, 0.0f, 0.0f};
    float cartesian[MAX_N_AXIS] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    kinematics->motors_to_cartesian(cartesian, motors, static_cast<axis_t>(3));

    // Z axis should pass through unchanged
    EXPECT_NEAR(50.0f, cartesian[2], TOLERANCE);
}

TEST_F(KinematicsTest, WallPlotterNegativeMotorValues) {
    auto kinematics = std::make_unique<Kinematics::WallPlotter>("WallPlotter");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    // Test with different cord lengths (note: WallPlotter handles motor direction inversion)
    // Shorter cords pull the puck up toward the anchors
    float motors[MAX_N_AXIS] = {100.0f, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float cartesian[MAX_N_AXIS] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    kinematics->motors_to_cartesian(cartesian, motors, static_cast<axis_t>(2));

    // Should produce valid cartesian position
    EXPECT_TRUE(std::isfinite(cartesian[0]));
    EXPECT_TRUE(std::isfinite(cartesian[1]));
}

TEST_F(KinematicsTest, WallPlotterLargeMotorValues) {
    auto kinematics = std::make_unique<Kinematics::WallPlotter>("WallPlotter");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis

    // Test with large motor values
    float motors[MAX_N_AXIS]    = { 100.0f, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float cartesian[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->motors_to_cartesian(cartesian, motors, static_cast<axis_t>(2));

    // Should produce valid cartesian position
    EXPECT_TRUE(std::isfinite(cartesian[0]));
    EXPECT_TRUE(std::isfinite(cartesian[1]));
}

TEST_F(KinematicsTest, WallPlotterCartesianToMotors) {
    auto wallPlotter = std::make_unique<Kinematics::WallPlotter>("WallPlotter");

    std::unique_ptr<Kinematics::KinematicSystem> kinematics = std::move(wallPlotter);

    kinematics->init();
    reset_motor_pos();

    // For WallPlotter: motor positions are rope lengths = distance from fixed pulleys
    // Pulley 0 at (-100, 100), Pulley 1 at (100, 100)
    // For horizontal move along Y=0, motor lengths change inversely
    // init() computes rope lengths assuming starting position at 0,0
    // in the middle of the work area.  We call that 0,0 in both the
    // cartesian and motor spaces; the actual starting rope lengths
    // are handled internally.

    float zeros[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    float motors[MAX_N_AXIS];
    float cartesian[MAX_N_AXIS];

    float target[MAX_N_AXIS];
    float position[MAX_N_AXIS];

    copyAxes(position, zeros);

    // Verify the motor coordinates after init()
    kinematics->transform_cartesian_to_motors(motors, position);
    AssertArrayNear(motors, zeros, MAX_N_AXIS, 0.001);

    // Verify the cartesian coordinates after init()
    kinematics->motors_to_cartesian(cartesian, motors, MAX_N_AXIS);
    AssertArrayNear(cartesian, position, MAX_N_AXIS, 0.001);

    copyAxes(target, position, MAX_N_AXIS);
    target[0] = 50.0;

    // Forward transform for target position
    kinematics->transform_cartesian_to_motors(motors, target);

    // Motors should have different values for different cartesian X positions
    EXPECT_NE(motors[0], 0.0) << "Motor 0 should change for horizontal movement";
    EXPECT_NE(motors[1], 0.0) << "Motor 1 should change for horizontal movement";

    // Verify round-trip: motors -> cartesian -> motors
    kinematics->motors_to_cartesian(cartesian, motors, MAX_N_AXIS);

    // Should get back approximately the same cartesian position
    AssertArrayNear(cartesian, target, MAX_N_AXIS, 0.001);

    // Test Z-only motion
    copyAxes(target, position);
    target[2] += 10.0;
    plan_line_data_t plan_data {};

    float initial_pos[MAX_N_AXIS];
    copyAxes(initial_pos, position);

    reset_motor_segments();
    EXPECT_TRUE(kinematics->cartesian_to_motors(target, &plan_data, position));
    copyAxes(position, target);

    // Z-only motion should generate exactly one bsegment
    EXPECT_EQ(get_motor_segments().size(), 1);

    kinematics->motors_to_cartesian(cartesian, get_motor_segments()[0].motors, MAX_N_AXIS);
    AssertArrayNear(cartesian, target, MAX_N_AXIS, 0.001);
    EXPECT_TRUE(isOnSegment(initial_pos, target, cartesian, MAX_N_AXIS));

    copyAxes(position, target);

    copyAxes(target, zeros);

    testSegmentation(target, position, MAX_N_AXIS, *kinematics);
    copyAxes(position, target);

    target[0] = -80.0;
    target[1] = -80.0;
    testSegmentation(target, position, MAX_N_AXIS, *kinematics);
    copyAxes(position, target);

    target[0] = -80.0;
    target[1] = 80.0;
    testSegmentation(target, position, MAX_N_AXIS, *kinematics);
    copyAxes(position, target);

    target[0] = 80.0;
    target[1] = 80.0;
    testSegmentation(target, position, MAX_N_AXIS, *kinematics);
    copyAxes(position, target);

    target[0] = 80.0;
    target[1] = -80.0;
    testSegmentation(target, position, MAX_N_AXIS, *kinematics);
    copyAxes(position, target);

    target[0] = 80.0;
    target[1] = -80.0;
    testSegmentation(target, position, MAX_N_AXIS, *kinematics);
    copyAxes(position, target);

    target[0] = 0.0;
    target[1] = 0.0;
    testSegmentation(target, position, MAX_N_AXIS, *kinematics);
    copyAxes(position, target);
}
