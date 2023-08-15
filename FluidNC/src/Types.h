#pragma once

#include <cstdint>

typedef uint32_t MotorMask;  // Bits indexed by motor_num*16 + axis
typedef uint16_t AxisMask;   // Bits indexed by axis number
typedef uint8_t  Percent;    // Integer percent

// System states. The state variable primarily tracks the individual functions
// to manage each without overlapping. It is also used as a messaging flag for
// critical events.
enum class State : uint8_t {
    Idle = 0,     // Must be zero.
    Alarm,        // In alarm state. Locks out all g-code processes. Allows settings access.
    CheckMode,    // G-code check mode. Locks out planner and motion only.
    Homing,       // Performing homing cycle
    Cycle,        // Cycle is running or motions are being executed.
    Hold,         // Active feed hold
    Jog,          // Jogging mode.
    SafetyDoor,   // Safety door is ajar. Feed holds and de-energizes system.
    Sleep,        // Sleep state.
    ConfigAlarm,  // You can't do anything but fix your config file.
    Critical,     // You can't do anything but reset with CTRL-x or the reset button
};
