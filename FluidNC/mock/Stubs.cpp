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
#include "Protocol.h"  // For NoArgEvent

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

void reset_motor_segments() {
    g_motor_segments.clear();
}

std::vector<MotorSegment>& get_motor_segments() {
    return g_motor_segments;
}

bool mc_move_motors(float* target, plan_line_data_t* plan_data) {
    // Capture motor coordinates for testing
    MotorSegment segment;
    for (size_t i = 0; i < 6; i++) {  // MAX_N_AXIS
        segment.motors[i] = target[i];
    }
    g_motor_segments.push_back(segment);
    // DEBUG: This should execute if stub is being called
    // ( Should be visible as captured segment )
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

float vector_distance(float* a, float* b, size_t size) {
    float dist_sq = 0;
    for (size_t i = 0; i < size; i++) {
        float diff = a[i] - b[i];
        dist_sq += diff * diff;
    }
    return sqrtf(dist_sq);
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
