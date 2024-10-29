// Copyright (c) 2024 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Stepping engine that uses direct GPIO accesses timed by spin loops.

#include "Driver/step_engine.h"
#include "Driver/fluidnc_gpio.h"
#include "Driver/delay_usecs.h"
#include "Driver/StepTimer.h"
#include <esp32-hal-gpio.h>
#include <esp_attr.h>  // IRAM_ATTR

static uint32_t _pulse_delay_us;
static uint32_t _dir_delay_us;

static uint32_t init_engine(uint32_t dir_delay_us, uint32_t pulse_delay_us, uint32_t frequency, bool (*callback)(void)) {
    stepTimerInit(frequency, callback);
    _dir_delay_us   = dir_delay_us;
    _pulse_delay_us = pulse_delay_us;
    return _pulse_delay_us;
}

static int init_step_pin(int step_pin, int step_invert) {
    return step_pin;
}

static int _stepPulseEndTime;

static void IRAM_ATTR set_pin(int pin, int level) {
    gpio_write(pin, level);
}

static void IRAM_ATTR finish_dir() {
    delay_us(_dir_delay_us);
}

static void IRAM_ATTR start_step() {}

// Instead of waiting here for the step end time, we mark when the
// step pulse should end, then return.  The stepper code can then do
// some work that is overlapped with the pulse time.  The spin loop
// will happen in start_unstep()
static void IRAM_ATTR finish_step() {
    _stepPulseEndTime = usToEndTicks(_pulse_delay_us);
}

static int IRAM_ATTR start_unstep() {
    spinUntil(_stepPulseEndTime);
    return 0;
}

// This is a noop because each gpio_write() takes effect immediately,
// so there is no need to commit multiple GPIO changes.
static void IRAM_ATTR finish_unstep() {}

static uint32_t max_pulses_per_sec() {
    return 1000000 / (2 * _pulse_delay_us);
}

static void IRAM_ATTR set_timer_ticks(uint32_t ticks) {
    stepTimerSetTicks(ticks);
}

static void IRAM_ATTR start_timer() {
    stepTimerStart();
}

static void IRAM_ATTR stop_timer() {
    stepTimerStop();
}

// clang-format off
static step_engine_t engine = {
    "Timed",
    init_engine,
    init_step_pin,
    set_pin,
    finish_dir,
    start_step,
    set_pin,
    finish_step,
    start_unstep,
    finish_unstep,
    max_pulses_per_sec,
    set_timer_ticks,
    start_timer,
    stop_timer
};

REGISTER_STEP_ENGINE(Timed, &engine);
