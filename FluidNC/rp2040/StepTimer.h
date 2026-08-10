// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Timer frequency
extern const uint32_t fTimerRP2040;

// Attach a callback for the step timer ISR
// The callback should return true if it wants to continue (reschedule), false to stop
void stepTimerAttach(bool (*callback)(void));

// Start the step timer
void stepTimerStart();

// Set the timer to fire after 'ticks' timer units
void stepTimerSetTicks(uint32_t ticks);

// Stop the timer
void stepTimerStop();

// Get current timer ticks
uint32_t stepTimerGetTicks();

#ifdef __cplusplus
}
#endif
