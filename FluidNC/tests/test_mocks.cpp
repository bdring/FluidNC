#include "test_mocks.h"
#include "Types.h"  // For axis_t, MAX_N_AXIS
#include "Logging.h"  // For MsgLevel type
#include <cmath>

// Stub implementation that returns false to suppress logging
bool atMsgLevel(MsgLevel level) {
    return false;
}

// ============================================================================
// Global Variables Implementation
// ============================================================================

float _min_motor_pos[MAX_N_AXIS] = { 0 };
float _max_motor_pos[MAX_N_AXIS] = { 0 };

// ============================================================================
// Axes Implementation
// ============================================================================

axis_t Machine::Axes::_numberAxis = MAX_N_AXIS;

Machine::Axis* Machine::Axes::_axis[MAX_N_AXIS] = { nullptr };  // MAX_N_AXIS
const char* Machine::Axes::_axisNames[] = {"X", "Y", "Z", "A", "B", "C"};
uint8_t Machine::Axes::homingMask = 0;
uint8_t Machine::Axes::negLimitMask = 0;
uint8_t Machine::Axes::posLimitMask = 0;

// Stub motor objects for axis access
static Machine::Motor _stub_motors[MAX_N_AXIS][Machine::Axis::MAX_MOTORS_PER_AXIS];

// Simple test-only proxy struct for MachineConfig
// Provides the same interface as the real MachineConfig without including dependencies
struct TestMachineConfig {
    Machine::Axes* _axes = nullptr;
};

// Wrapper class to provide an Axes instance interface for the mock static Axes
class TestAxes : public Machine::Axes {
public:
    // This instance serves as the config->_axes pointer
    // It inherits the static members from Machine::Axes
};

static TestAxes _test_axes_instance;

void Machine::Axes::init_stubs(axis_t num_axes) {
    _numberAxis = num_axes;
    for (size_t i = 0; i < num_axes; i++) {
        if (!_axis[i]) {
            _axis[i] = new Axis();
        }
        // Initialize motor pointers to stub motors
        for (size_t j = 0; j < Axis::MAX_MOTORS_PER_AXIS; j++) {
            _axis[i]->_motors[j] = &_stub_motors[i][j];
        }
    }

    // Initialize configuration infrastructure
    // The global 'config' pointer must point to a valid MachineConfig instance
    // Use a static instance that persists as long as the program runs
    static TestMachineConfig _static_test_config;
    _static_test_config._axes = &_test_axes_instance;

    // Point the extern config pointer to this instance
    // This works because TestMachineConfig has compatible layout with MachineConfig
    // and we're only using the _axes member which is guaranteed to be first
    config = (Machine::MachineConfig*)(&_static_test_config);
}

void Machine::Axes::cleanup_stubs() {
    for (size_t i = 0; i < 6; i++) {
        delete _axis[i];
        _axis[i] = nullptr;
    }
    // Clean up the test config reference
    config = nullptr;
}

// ============================================================================
// Axis Instance Methods
// ============================================================================

bool Machine::Axis::can_home() {
    return true;  // No-op stub returning true
}

void Machine::Axis::extraPulloff() {
    // No-op stub
}

void Machine::Axis::commonPulloff() {
    // No-op stub
}

// ============================================================================
// Motor Implementation
// ============================================================================

void Machine::Motor::limitOtherAxis(axis_t axis) {
    // No-op stub for CoreXY initialization
}

// ============================================================================
// Limit Functions Implementation
// ============================================================================

float limitsMinPosition(axis_t axis) {
    if (axis < Machine::Axes::_numberAxis && Machine::Axes::_axis[axis]) {
        return Machine::Axes::_axis[axis]->_min;
    }
    return -1000.0f;
}

float limitsMaxPosition(axis_t axis) {
    if (axis < Machine::Axes::_numberAxis && Machine::Axes::_axis[axis]) {
        return Machine::Axes::_axis[axis]->_max;
    }
    return 1000.0f;
}

void set_steps(axis_t axis, int32_t steps) {
    // No-op for testing
}

void limit_error() {
    // No-op for testing - could log but we're keeping it minimal
}

void limit_error(axis_t axis, float position) {
    // No-op for testing - could log but we're keeping it minimal
}

void protocol_disable_steppers() {
    // No-op for testing
}

void Machine::Axes::set_disable(bool disable) {
    // No-op for testing
}

// ============================================================================
// System State Stub
// ============================================================================
// ParallelDelta uses sys.abort() - provide a stub that returns false
struct system_stub_t {
    bool abort() const { return false; }  // Tests should never abort
};

// Global system object - name it sys to match System.h declaration
// Use C linkage to match the symbol name ParallelDelta is looking for
extern "C" {
    system_stub_t sys;
}


