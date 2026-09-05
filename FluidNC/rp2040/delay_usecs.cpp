// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// RP2040 microsecond delay implementation using hardware timer

#include "Driver/delay_usecs.h"
#include "hardware/timer.h"
#include "pico/stdlib.h"

// On RP2040, the timer runs at 1 MHz, so 1 tick = 1 microsecond
// Therefore, ticks_per_us = 1
uint32_t ticks_per_us = 1;

// Initialize timing subsystem
void timing_init() {
    // RP2040 timer is always running at 1 MHz
    // No initialization needed, but we set ticks_per_us for consistency
    ticks_per_us = 1;
}

// Get the current CPU ticks from the hardware timer
// Returns the 32-bit lower part of the 64-bit timer counter
int32_t getCpuTicks() {
    return (int32_t)timer_hw->timerawl;
}

// Convert microseconds to CPU ticks
// At 1 MHz, this is simply: us * 1
int32_t usToCpuTicks(int32_t us) {
    return us * ticks_per_us;
}

// Convert microseconds to absolute end time (current time + us)
int32_t usToEndTicks(int32_t us) {
    return getCpuTicks() + usToCpuTicks(us);
}

// Busy-wait (spin) until the specified tick count is reached
void spinUntil(int32_t endTicks) {
    while ((getCpuTicks() - endTicks) < 0) {
        tight_loop_contents();  // Let other hardware finish in the meantime
    }
}

// Blocking delay for very short time intervals
void delay_us(int32_t us) {
    if (us > 0) {
        spinUntil(usToEndTicks(us));
    }
}

// Delay for a specified number of microseconds (alternative API)
void delay_usecs(uint32_t usecs) {
    delay_us((int32_t)usecs);
}

// Higher precision delay for very small intervals
void delay_usecs_precision(uint32_t usecs) {
    // RP2040 timer precision is 1 microsecond, so this is the same as delay_usecs
    delay_usecs(usecs);
}
