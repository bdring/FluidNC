#pragma once

// Forward declaration - the actual class is defined in Machine/MachineConfig.h
// which is included in the test implementation files
namespace Machine {
    class MachineConfig;
}

// Minimal mock implementations for testing kinematics with real source code

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <iostream>
#include <iomanip>

// Include mock Channel first - Logging.h will use it
#include "Channel.h"

// ============================================================================
// Axis and Motor Stubs
// ============================================================================

namespace Machine {
    class Motor;  // Forward declaration
    
    struct Axis {
        static const int MAX_MOTORS_PER_AXIS = 2;
        
        bool _softLimits = false;
        float _min = -1000.0f;
        float _max = 1000.0f;
        Motor* _motors[MAX_MOTORS_PER_AXIS] = {nullptr, nullptr};

        // Methods called by kinematics code
        bool can_home();
        void extraPulloff();
        void commonPulloff();
    };

    class Axes {
    public:
        static axis_t      _numberAxis;
        static Axis*       _axis[MAX_N_AXIS];  // MAX_N_AXIS from Types.h
        static const char* _axisNames[];
        static uint8_t     homingMask;
        static uint8_t     negLimitMask;
        static uint8_t     posLimitMask;

        static void init_stubs(axis_t num_axes);  // Defined in test_mocks.cpp

        static void cleanup_stubs();  // Defined in test_mocks.cpp

        static const char* axisName(axis_t axis) {
            if (axis < MAX_N_AXIS) {
                return _axisNames[axis];
            }
            return "?";
        }

        static void set_disable(bool disable);  // Stub for ParallelDelta kinematics
    };

    class Motor {
    public:
        void limitOtherAxis(axis_t axis);  // Stub for kinematics code
    };
}

// Global config instance - declare as extern, defined in test_mocks.cpp
extern Machine::MachineConfig* config;

// ============================================================================
// Motor Position Arrays for Soft Limits
// ============================================================================

extern float _min_motor_pos[MAX_N_AXIS];
extern float _max_motor_pos[MAX_N_AXIS];

// ============================================================================
// Stub Functions for Soft Limits
// ============================================================================

float limitsMinPosition(uint8_t axis);
float limitsMaxPosition(uint8_t axis);
void set_steps(uint8_t axis, int32_t steps);
void limit_error();  // No-argument version
void limit_error(uint8_t axis, float position);  // Two-argument version

// ============================================================================
// ParallelDelta Dependencies
// ============================================================================

float* get_motor_pos();
void set_motor_pos(float* pos, size_t n_axis);
void set_motor_pos(size_t axis, float pos);

float  vector_length(const float* v, size_t n);
float  vector_distance(const float* v1, const float* v2, size_t n);

void protocol_disable_steppers();

// Forward declaration for plan_line_data_t
struct plan_line_data_t;

// Motion planner stub
bool mc_move_motors(float* motors, plan_line_data_t* pl_data);

// ============================================================================
// Test Helpers for cartesian_to_motors Testing
// ============================================================================

#include <vector>

struct MotorSegment {
    // Array must be sized MAX_N_AXIS and the calling test must initialize ALL elements (with zeros if needed).
    // The mc_move_motors() stub uses vector_distance(motors, ..., MAX_N_AXIS) to compute segment_length.
    // Uninitialized elements = inf/nan in distance calculations. Tests MUST use MAX_N_AXIS sized arrays.
    float motors[MAX_N_AXIS];
    float segment_length;  // Distance traveled in motor coordinates (mm)
    float segment_time;    // Time to execute segment (seconds)
};

// Reset captured segments
void reset_motor_segments();
void reset_motor_pos();

// Get captured motor segments from mc_move_motors calls
std::vector<MotorSegment>& get_motor_segments();

template <typename D, typename S>
void copyAxes(D* dest, S* src, axis_t n_axis) {
    for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
        dest[axis] = src[axis];
    }
}

template <typename D, typename S>
void copyAxes(D* dest, S* src) {
    copyAxes(dest, src, Machine::Axes::_numberAxis);
}

template <typename D, typename S>
void addAxes(D* dest, S* src, axis_t n_axis) {
    for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
        dest[axis] += src[axis];
    }
}

template <typename D, typename S>
void addAxes(D* dest, S* src) {
    addAxes(dest, src, Machine::Axes::_numberAxis);
}

template <typename D, typename S>
void subtractAxes(D* dest, S* src, axis_t n_axis) {
    for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
        dest[axis] -= src[axis];
    }
}

template <typename D, typename S>
void subtractAxes(D* dest, S* src) {
    subtractAxes(dest, src, Machine::Axes::_numberAxis);
}
// Print a float[MAX_N_AXIS] array to std::cout in format: [x y z a b c] rounded to 2 decimal places
inline void printAxes(const float* axes, const char* label = "", int precision = 2) {
    if (label && label[0] != '\0') {
        std::cout << label << ": ";
    }
    std::cout << "[" << std::fixed << std::setprecision(precision);
    for (size_t i = 0; i < MAX_N_AXIS; ++i) {
        std::cout << axes[i];
        if (i < MAX_N_AXIS - 1)
            std::cout << " ";
    }
    std::cout << "]" << std::endl;
}
