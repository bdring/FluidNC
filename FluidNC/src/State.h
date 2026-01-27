#pragma once

#include <cstdint>

// System states. The state variable primarily tracks the individual functions
// to manage each without overlapping. It is also used as a messaging flag for
// critical events.
enum class State : uint8_t {
    Idle = 0,     // Must be zero.
    Alarm,        // In alarm state. Locks out all g-code processes. Allows settings access.
    CheckMode,    // G-code check mode. Locks out planner and motion only.
    Homing,       // Performing homing cycle
    Cycle,        // Cycle is running or motions are being executed.
    Hold,         // Initiating feedhold (decelerating)
    Held,         // Feedhold complete
    Jog,          // Jogging mode.
    SafetyDoor,   // Safety door is ajar. Feed holds and de-energizes system.
    Sleep,        // Sleep state.
    ConfigAlarm,  // You can't do anything but fix your config file.
    Critical,     // You can't do anything but reset with CTRL-x or the reset button
    Starting,     // Initial startup
};

void set_state(State s);
bool state_is(State s);
