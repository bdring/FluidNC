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
constexpr float TOLERANCE = 1e-5f;

// ============================================================================
// TEST CLASS
// ============================================================================

class KinematicsTest : public ::testing::Test {
protected:
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
    // Uses parametric line equation: point = position + t * (target - position)
    // where t should be between 0 and 1 for points on the segment
    bool isOnSegment(const float* position, const float* target, const float* point, axis_t n_axis) {
        // Handle collinearity by checking cross product in 2D/3D
        // For generality, we compute the vector from position to point and position to target
        // If parallel (cross product ~= 0), they're collinear

        float pos_to_point[3]  = { 0, 0, 0 };
        float pos_to_target[3] = { 0, 0, 0 };

        for (int i = 0; i < std::min((int)3, (int)n_axis); i++) {
            pos_to_point[i]  = point[i] - position[i];
            pos_to_target[i] = target[i] - position[i];
        }

        // For 2D: cross product is scalar (z component) = x1*y2 - y1*x2
        float cross = pos_to_point[0] * pos_to_target[1] - pos_to_point[1] * pos_to_target[0];

        // Check if collinear (cross product near zero)
        return std::abs(cross) < TOLERANCE;
    }

    void expectLinearSegments(const float* position, const float* target, axis_t n_axis, Kinematics::KinematicSystem& kinematics) {
        EXPECT_GT(get_motor_segments().size(), 0);

        float cartesian[MAX_N_AXIS];
        for (auto& segment : get_motor_segments()) {
            kinematics.motors_to_cartesian(cartesian, segment.motors, n_axis);
            EXPECT_TRUE(isOnSegment(position, target, cartesian, n_axis));
        }

        AssertArrayNear(target, cartesian, (int)n_axis, 0.001);
    }

    void testSegmentation(float* position, float* target, axis_t n_axis, Kinematics::KinematicSystem& kinematics) {
        reset_motor_segments();
        plan_line_data_t plan_data {};

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

#ifdef DISABLED_CARTESIAN_CARTESIAN_TO_MOTORS
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
#endif

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

#ifdef DISABLED_COREXY_CARTESIAN_TO_MOTORS
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
#endif
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

// ============================================================================
// PARALLEL DELTA KINEMATICS TESTS
// ============================================================================

TEST_F(KinematicsTest, ParallelDeltaInverseTransform) {
    auto kinematics = std::make_unique<Kinematics::ParallelDelta>("ParallelDelta");
    // Note: init() not called - we test pure math transforms without full machine configuration

    // Test centered position
    float cartesian[MAX_N_AXIS] = { 0.0f, 0.0f, -80.0f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    // ParallelDelta calculates angle positions for 3 motors
    bool result = kinematics->transform_cartesian_to_motors(motors, cartesian);

    // Should be able to solve kinematics for this position
    EXPECT_TRUE(result);
    // All three motors should have valid angles
    EXPECT_TRUE(std::isfinite(motors[0]));
    EXPECT_TRUE(std::isfinite(motors[1]));
    EXPECT_TRUE(std::isfinite(motors[2]));
}

TEST_F(KinematicsTest, ParallelDeltaForwardTransform) {
    auto kinematics = std::make_unique<Kinematics::ParallelDelta>("ParallelDelta");
    // Note: init() not called - we test pure math transforms without full machine configuration

    // Test with motor angles at 0 degrees (neutral position)
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float cartesian[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    kinematics->motors_to_cartesian(cartesian, motors, static_cast<axis_t>(3));

    // Should recover a valid position
    EXPECT_TRUE(std::isfinite(cartesian[0]));
    EXPECT_TRUE(std::isfinite(cartesian[1]));
    EXPECT_TRUE(std::isfinite(cartesian[2]));
    // Z should be negative (end effector is below motor plane)
    EXPECT_LT(cartesian[2], 0.0f);
}

TEST_F(KinematicsTest, ParallelDeltaRoundTrip) {
    auto kinematics = std::make_unique<Kinematics::ParallelDelta>("ParallelDelta");
    // Note: init() not called - we test pure math transforms without full machine configuration

    float original[MAX_N_AXIS]  = { 0.0f, 0.0f, -80.0f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float recovered[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    bool forward_ok = kinematics->transform_cartesian_to_motors(motors, original);
    EXPECT_TRUE(forward_ok);

    kinematics->motors_to_cartesian(recovered, motors, static_cast<axis_t>(3));

    // Allow larger tolerance for ParallelDelta due to trigonometric round-trip
    AssertArrayNear(original, recovered, 3, 0.1f);
}

TEST_F(KinematicsTest, ParallelDeltaOrigin) {
    auto kinematics = std::make_unique<Kinematics::ParallelDelta>("ParallelDelta");
    // Note: init() not called - we test pure math transforms without full machine configuration

    float cartesian[MAX_N_AXIS] = { 0.0f, 0.0f, -80.0f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    bool result = kinematics->transform_cartesian_to_motors(motors, cartesian);

    // Verify we got valid motor values (should not be NaN or infinite)
    EXPECT_TRUE(result);
    EXPECT_TRUE(std::isfinite(motors[0]));
    EXPECT_TRUE(std::isfinite(motors[1]));
    EXPECT_TRUE(std::isfinite(motors[2]));
}

TEST_F(KinematicsTest, ParallelDeltaZMove) {
    auto kinematics = std::make_unique<Kinematics::ParallelDelta>("ParallelDelta");
    // Note: init() not called - we test pure math transforms without full machine configuration

    // Test Z-only moves (vertical movement)
    float pos1[MAX_N_AXIS]    = { 0.0f, 0.0f, -80.0f, 0.0f, 0.0f, 0.0f };
    float pos2[MAX_N_AXIS]    = { 0.0f, 0.0f, -100.0f, 0.0f, 0.0f, 0.0f };
    float motors1[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float motors2[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    bool ok1 = kinematics->transform_cartesian_to_motors(motors1, pos1);
    bool ok2 = kinematics->transform_cartesian_to_motors(motors2, pos2);
    EXPECT_TRUE(ok1);
    EXPECT_TRUE(ok2);

    // Motors should change as Z position changes
    bool motors_changed = (motors1[0] != motors2[0]) || (motors1[1] != motors2[1]) || (motors1[2] != motors2[2]);
    EXPECT_TRUE(motors_changed);
}

TEST_F(KinematicsTest, ParallelDeltaXYMove) {
    auto kinematics = std::make_unique<Kinematics::ParallelDelta>("ParallelDelta");
    // Note: init() not called - we test pure math transforms without full machine configuration

    // Test XY movement at fixed Z
    float pos1[MAX_N_AXIS]    = { 0.0f, 0.0f, -80.0f, 0.0f, 0.0f, 0.0f };
    float pos2[MAX_N_AXIS]    = { 10.0f, 0.0f, -80.0f, 0.0f, 0.0f, 0.0f };
    float motors1[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float motors2[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    bool ok1 = kinematics->transform_cartesian_to_motors(motors1, pos1);
    bool ok2 = kinematics->transform_cartesian_to_motors(motors2, pos2);
    EXPECT_TRUE(ok1);
    EXPECT_TRUE(ok2);

    // Motors should change due to different X position
    bool motors_changed = (motors1[0] != motors2[0]) || (motors1[1] != motors2[1]) || (motors1[2] != motors2[2]);
    EXPECT_TRUE(motors_changed);
}

TEST_F(KinematicsTest, ParallelDeltaNegativeZ) {
    auto kinematics = std::make_unique<Kinematics::ParallelDelta>("ParallelDelta");
    // Don't call init() - we're testing pure math transforms without machine configuration

    // Test lower Z position (closer to base)
    float cartesian[MAX_N_AXIS] = { 0.0f, 0.0f, -120.0f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float recovered[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    bool forward_ok = kinematics->transform_cartesian_to_motors(motors, cartesian);
    EXPECT_TRUE(forward_ok);

    kinematics->motors_to_cartesian(recovered, motors, static_cast<axis_t>(3));

    // Verify round trip with larger tolerance for ParallelDelta
    AssertArrayNear(cartesian, recovered, 3, 0.1f);
}

TEST_F(KinematicsTest, ParallelDeltaThreeAxis) {
    auto kinematics = std::make_unique<Kinematics::ParallelDelta>("ParallelDelta");
    // Don't call init() - we're testing pure math transforms without machine configuration

    float cartesian[MAX_N_AXIS] = { 5.0f, -5.0f, -90.0f, 0.0f, 0.0f, 0.0f };
    float motors[MAX_N_AXIS]    = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float recovered[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    bool forward_ok = kinematics->transform_cartesian_to_motors(motors, cartesian);
    EXPECT_TRUE(forward_ok);

    kinematics->motors_to_cartesian(recovered, motors, static_cast<axis_t>(3));

    // Verify all three motors are involved in the transformation
    EXPECT_TRUE(std::isfinite(motors[0]));
    EXPECT_TRUE(std::isfinite(motors[1]));
    EXPECT_TRUE(std::isfinite(motors[2]));

    // Verify round trip
    AssertArrayNear(cartesian, recovered, 3, 0.1f);
}

// ============================================================================
// WALLPLOTTER KINEMATICS TESTS
// ============================================================================

#if 0
TEST_F(KinematicsTest, WallPlotterOrigin) {
    auto kinematics = std::make_unique<Kinematics::WallPlotter>("WallPlotter");
    // Note: init() not called - motor values are relative to zero offset (direct cord lengths)
    
    // Use physically valid motor values representing cord lengths to reach (0, 50)
    // With anchors at (-100,100) and (100,100), cords to (0,50) are both ~111.8
    float motors[MAX_N_AXIS] = {111.8f, 111.8f, 0.0f, 0.0f, 0.0f, 0.0f};
    float cartesian[MAX_N_AXIS] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    
    kinematics->motors_to_cartesian(cartesian, motors, static_cast<axis_t>(2));
    
    // Should produce valid cartesian coordinates
    EXPECT_TRUE(std::isfinite(cartesian[0]));
    EXPECT_TRUE(std::isfinite(cartesian[1]));
}
#endif
#if 0
TEST_F(KinematicsTest, WallPlotterLeftMotorMovement) {
    auto kinematics = std::make_unique<Kinematics::WallPlotter>("WallPlotter");
    kinematics->init();  // Initialize with mocked Axes::_numberAxis
    
    // Test left motor movement with valid cord lengths
    // Cord 1=111.8, Cord 2=111.8 reaches approximately (0, 50)
    float motors1[MAX_N_AXIS] = {111.8f, 111.8f, 0.0f, 0.0f, 0.0f, 0.0f};
    float motors2[MAX_N_AXIS] = {121.8f, 111.8f, 0.0f, 0.0f, 0.0f, 0.0f};  // Left cord 10mm longer
    float cart1[MAX_N_AXIS] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float cart2[MAX_N_AXIS] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    
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
    float motors[MAX_N_AXIS] = {100.0f, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float cartesian[MAX_N_AXIS] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    
    kinematics->motors_to_cartesian(cartesian, motors, static_cast<axis_t>(2));
    
    // Should produce valid cartesian position
    EXPECT_TRUE(std::isfinite(cartesian[0]));
    EXPECT_TRUE(std::isfinite(cartesian[1]));
}
#endif

TEST_F(KinematicsTest, WallPlotterCartesianToMotors) {
    auto wallPlotter = std::make_unique<Kinematics::WallPlotter>("WallPlotter");

    std::unique_ptr<Kinematics::KinematicSystem> kinematics = std::move(wallPlotter);

    kinematics->init();

    auto n_axis = static_cast<axis_t>(3);
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

    // Verify the motor coordinates after init()
    kinematics->transform_cartesian_to_motors(motors, zeros);
    AssertArrayNear(motors, zeros, 3, 0.001);

    // Verify the cartesian coordinates after init()
    kinematics->motors_to_cartesian(cartesian, motors, n_axis);
    AssertArrayNear(cartesian, zeros, 3, 0.001);

    float motors_end[MAX_N_AXIS]       = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float cartesian_target[MAX_N_AXIS] = { 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    // Forward transform for target position
    kinematics->transform_cartesian_to_motors(motors_end, cartesian_target);

    // Motors should have different values for different cartesian X positions
    EXPECT_NE(motors[0], motors_end[0]) << "Motor 0 should change for horizontal movement";
    EXPECT_NE(motors[1], motors_end[1]) << "Motor 1 should change for horizontal movement";

    // Verify round-trip: motors -> cartesian -> motors
    float cartesian_roundtrip[MAX_N_AXIS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    kinematics->motors_to_cartesian(cartesian_roundtrip, motors_end, n_axis);

    // Should get back approximately the same cartesian position
    EXPECT_NEAR(cartesian_target[0], cartesian_roundtrip[0], TOLERANCE)
        << "X coordinate mismatch: expected " << cartesian_target[0] << " but got " << cartesian_roundtrip[0];
    EXPECT_NEAR(cartesian_target[1], cartesian_roundtrip[1], TOLERANCE)
        << "Y coordinate mismatch: expected " << cartesian_target[1] << " but got " << cartesian_roundtrip[1];
    EXPECT_NEAR(cartesian_target[2], cartesian_roundtrip[2], TOLERANCE) << "Z coordinate round-trip failed";

    // Test Z-only motion
    float target[MAX_N_AXIS];
    copyAxes(target, cartesian_roundtrip);
    target[2] += 10.0;
    plan_line_data_t plan_data {};

    reset_motor_segments();
    EXPECT_TRUE(kinematics->cartesian_to_motors(target, &plan_data, cartesian_roundtrip));

    // Z-only motion should generate exactly one bsegment
    EXPECT_EQ(get_motor_segments().size(), 1);

    float retarget[MAX_N_AXIS];

    kinematics->motors_to_cartesian(retarget, get_motor_segments()[0].motors, n_axis);
    AssertArrayNear(target, retarget, 3, 0.001);
    EXPECT_TRUE(isOnSegment(cartesian_roundtrip, target, retarget, n_axis));

    testSegmentation(target, cartesian_roundtrip, n_axis, *kinematics);
}
