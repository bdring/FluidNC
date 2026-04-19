// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// RP2040 Watchdog Timer driver

#include "hardware/watchdog.h"
#include <stdint.h>

// Initialize watchdog timer
void init_watchdog(uint32_t timeout_ms) {
    // Note: RP2040 watchdog timeout is in microseconds
    watchdog_enable(timeout_ms * 1000, false);  // false = don't call reset_to_usb_boot
}

// Feed (reset) the watchdog timer
void feed_watchdog() {
    watchdog_update();
}

// Disable watchdog timer
void disable_watchdog() {
    // RP2040 watchdog cannot be disabled after being enabled
    // This is a hardware limitation for safety
}

// Get watchdog status
bool watchdog_is_enabled() {
    // RP2040 doesn't provide a direct way to check if watchdog is enabled
    // This would typically be tracked in firmware
    return true;  // Assumed enabled if init_watchdog was called
}

void add_watchdog_to_task() {}
