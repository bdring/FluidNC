#include "StatusLed.h"

/*

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
};

*/

StatusLed::StatusLed() : pixels(1, 45, NEO_GRB + NEO_KHZ800) {}

void StatusLed::init() {
    pixels.begin();
    pixels.clear();
    pixels.setPixelColor(0, pixels.Color(64, 0, 0));
    pixels.show();
}

// TODO FIXME: Added alarm = red, etc.

void StatusLed::update() {
    //if (Uart0.isConnected()) {
    pixels.setPixelColor(0, pixels.Color(0, 64, 64));
    //} else {
    //    pixels.setPixelColor(0, pixels.Color(0, 64, 0));
    //}
    pixels.show();
}

StatusLed statusLed;
