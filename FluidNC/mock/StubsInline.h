// Inline stub implementations for kinematics testing
// These provide forward declarations and stub implementations for functions
// called by kinematics code that aren't available in test environment

#pragma once

#include <cstdint>
#include <cstddef>

// Forward declarations for types that kinematics uses
class Event;

namespace Machine {
    // ExecAlarm enum forward declaration
    enum class ExecAlarm : uint8_t;
}

// ============================================================================
// Logging Stubs
// ============================================================================

bool atMsgLevel(int level);

// ============================================================================
// Motion & Limits Stubs
// ============================================================================

bool mc_move_motors(float* target, struct plan_line_data_t* plan_data);
void limits_get_state();
bool ambiguousLimit();

// ============================================================================
// Math & Position Stubs
// ============================================================================

int32_t motor_pos_to_steps(float position, uint32_t stepCount);
void get_mpos();

// ============================================================================
// Protocol & Event Stubs
// ============================================================================

void protocol_send_event(const Event* event, void* arg);

// ============================================================================
// Assertion Stub
// ============================================================================

class AssertionFailed {
public:
    static void create(const char* format, ...);
};
