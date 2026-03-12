// Stub implementations for kinematics testing
// Provides no-op implementations for functions called by kinematics

#include <cstdint>
#include <cstddef>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <iostream>

// Include necessary headers for type definitions
#include "Types.h"
#include "System.h"  // For steps_t and motor_pos_to_steps declarations
#include "Machine/Homing.h"
#include "Stepping.h"
#include "Protocol.h"

// Get MotorSegment definition and function declarations from test mocks
#include "../tests/test_mocks.h"

struct plan_line_data_t {
    float feed_rate;
};

// ============================================================================
// Global Variables
// ============================================================================

const NoArgEvent cycleStartEvent { [](){} };  // No-op event handler

// For testing: store captured motor segments
std::vector<MotorSegment> g_motor_segments;
static float g_last_motor_pos[MAX_N_AXIS] = {0};  // Track previous position for segment length calculation

void reset_motor_pos() {
    for (size_t i = 0; i < MAX_N_AXIS; i++) {
        g_last_motor_pos[i] = 0.0f;
    }
}

void reset_motor_segments() {
    g_motor_segments.clear();
}

std::vector<MotorSegment>& get_motor_segments() {
    return g_motor_segments;
}

bool mc_move_motors(float* target, plan_line_data_t* plan_data) {
    // Capture motor coordinates for testing
    MotorSegment segment;

    // Copy current motor positions - must copy ALL MAX_N_AXIS elements to ensure no uninitialized data
    copyAxes(segment.motors, target, MAX_N_AXIS);

    // Calculate segment length (distance in motor coordinates)
    // CRITICAL: Must use MAX_N_AXIS here, not n_axis, because:
    // 1. The MotorSegment.motors array is always MAX_N_AXIS in size
    // 2. vector_distance() will read all MAX_N_AXIS elements
    // 3. If test callers didn't fully initialize their arrays, uninitialized elements = inf/nan
    // 4. Tests MUST use MAX_N_AXIS sized arrays initialized with zeros to avoid this
    segment.segment_length = vector_distance(target, g_last_motor_pos, MAX_N_AXIS);

    // Calculate segment time from feedrate
    // feedrate is in mm/min, segment_length is in mm
    // time (seconds) = segment_length (mm) / (feedrate (mm/min) / 60)
    // = segment_length * 60 / feedrate
    if (plan_data && plan_data->feed_rate > 0.0f) {
        segment.segment_time = (segment.segment_length * 60.0f) / plan_data->feed_rate;
    } else {
        segment.segment_time = 0.0f;
    }

    // Store the segment
    g_motor_segments.push_back(segment);

    // Update last position for next segment
    copyAxes(g_last_motor_pos, target, MAX_N_AXIS);

    return true;
}

void limits_get_state() {
    // No-op stub
}

bool ambiguousLimit() {
    return false;
}

// ============================================================================
// Math & Position Stubs
// ============================================================================

float hypot_f(float x, float y) {
    return sqrtf(x * x + y * y);
}

float vector_distance(const float* a, const float* b, size_t size) {
    float dist_sq = 0;
    for (size_t i = 0; i < size; i++) {
        float diff = a[i] - b[i];
        dist_sq += diff * diff;
    }
    return sqrtf(dist_sq);
}

float vector_length(const float* v, size_t n) {
    float sum = 0.0f;
    for (size_t i = 0; i < n; i++) {
        sum += v[i] * v[i];
    }
    return sqrtf(sum);
}

// Motor position tracking for ParallelDelta kinematics
static float _motor_positions[MAX_N_AXIS] = { 0 };

float* get_motor_pos() {
    return _motor_positions;
}

void set_motor_pos(float* pos, size_t n_axis) {
    for (size_t i = 0; i < n_axis && i < MAX_N_AXIS; i++) {
        _motor_positions[i] = pos[i];
    }
}

void set_motor_pos(size_t axis, float pos) {
    if (axis < MAX_N_AXIS) {
        _motor_positions[axis] = pos;
    }
}

// Overload 1: Per-axis motor position to steps conversion
steps_t motor_pos_to_steps(float mpos, size_t motor) {
    return (steps_t)mpos;  // Simple stub
}

// Overload 2: All axes motor position to steps conversion  
void motor_pos_to_steps(steps_t* steps, float* motor_pos) {
    // No-op stub - would copy motor positions to steps array
}

float* get_mpos() {
    static float mpos[6] = {0};
    return mpos;
}

// ============================================================================
// Protocol & Event Stubs  
// ============================================================================

void protocol_send_event(const Event* event, void* arg) {
    // No-op stub
}

// ============================================================================
// Machine Namespace Stubs
// ============================================================================

namespace Machine {
    void Homing::fail(ExecAlarm alarm) {
        // No-op stub
    }
    
    void Stepping::block(axis_t axis, uint8_t mask) {
        // No-op stub
    }
    
    void Stepping::unlimit(axis_t axis, uint8_t mask) {
        // No-op stub
    }
}

// ============================================================================
// Assertion Stub
// ============================================================================

std::runtime_error AssertionFailed::create(const char* msg, ...) {
    // Return a runtime_error without throwing
    // This should never actually trigger in tests
    return std::runtime_error("Assertion failed in kinematics");
}
