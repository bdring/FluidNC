// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Interface to the RP2040 timer for step timing
// Uses the hardware timer from the Pico SDK

#include "Platform.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include <stdint.h>
#include <stdbool.h>
#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

// Define ALARM_NUM if not already defined
#ifndef ALARM_NUM
#    define ALARM_NUM 0
#endif

// Select timer block and IRQ based on chip
#ifdef PICO_RP2350
#    define TIMER_IRQ_NUM TIMER0_IRQ_0
#else  // PICO_RP2040
#    define TIMER_IRQ_NUM TIMER_IRQ_0
#endif

// RP2040/RP2350 timer frequency: 1 MHz (1 microsecond ticks)
static const uint32_t fTimerRP2040 = 1000000;

static bool (*timer_isr_callback)(void)  = nullptr;
static volatile uint64_t last_alarm_time = 0;
// Next interval in ticks (microseconds)
static volatile uint32_t next_ticks = 0;

// ISR for the timer alarm
static void IRAM_ATTR timer_isr_handler() {
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    if (timer_isr_callback && timer_isr_callback()) {
        // Reschedule the next alarm using next_ticks
        uint32_t now               = timer_hw->timerawl;
        timer_hw->alarm[ALARM_NUM] = (uint32_t)(now + next_ticks);
        hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
    }
}

void stepTimerAttach(bool (*callback)(void)) {
    timer_isr_callback = callback;

    // Set up the alarm interrupt
    irq_set_enabled(TIMER_IRQ_NUM, false);
    irq_set_exclusive_handler(TIMER_IRQ_NUM, timer_isr_handler);
    irq_set_enabled(TIMER_IRQ_NUM, true);
}

void IRAM_ATTR stepTimerStart() {
    // Set initial alarm to fire very soon
    uint64_t target            = timer_hw->timerawl + 10;
    timer_hw->alarm[ALARM_NUM] = (uint32_t)target;
    hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
}

void IRAM_ATTR stepTimerSetTicks(uint32_t ticks) {
    // Set the next interval in ticks (microseconds)
    next_ticks = ticks;
}

void IRAM_ATTR stepTimerStop() {
    hw_clear_bits(&timer_hw->inte, 1u << ALARM_NUM);
}

uint32_t stepTimerGetTicks() {
    return timer_hw->timerawl;
}

#ifdef __cplusplus
}
#endif
