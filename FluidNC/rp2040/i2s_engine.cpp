// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Driver/step_engine.h"
#include "Driver/i2s_out.h"
#include "Driver/delay_usecs.h"
#include "StepTimer.h"

namespace {
    uint32_t pulse_delay_us = 2;
    uint32_t dir_delay_us   = 0;
    int32_t  step_pulse_end_time;

    uint32_t init_engine(uint32_t dir_delay, uint32_t pulse_delay, uint32_t frequency, bool (*callback)(void)) {
        (void)frequency;
        stepTimerAttach(callback);

        dir_delay_us   = dir_delay;
        pulse_delay_us = pulse_delay;
        return pulse_delay_us;
    }

    uint32_t init_step_pin(pinnum_t step_pin, bool step_invert) {
        i2s_out_write(step_pin, step_invert ? 1 : 0);
        return step_pin;
    }

    void set_dir_pin(pinnum_t pin, bool level) {
        i2s_out_write(pin, level ? 1 : 0);
    }

    void finish_dir() {
        i2s_out_delay();
        delay_us(dir_delay_us);
    }

    void start_step() {}

    void set_step_pin(pinnum_t pin, bool level) {
        i2s_out_write(pin, level ? 1 : 0);
    }

    void finish_step() {
        i2s_out_delay();
        step_pulse_end_time = usToEndTicks(pulse_delay_us);
    }

    bool start_unstep() {
        spinUntil(step_pulse_end_time);
        return false;
    }

    void finish_unstep() {
        i2s_out_delay();
    }

    uint32_t max_pulses_per_sec() {
        return 1000000 / (2 * pulse_delay_us);
    }

    void set_timer_ticks(uint32_t ticks) {
        stepTimerSetTicks(ticks);
    }

    void start_timer() {
        stepTimerStart();
    }

    void stop_timer() {
        stepTimerStop();
    }
}

static step_engine_t i2s_engine = {
    "I2S",
    init_engine,
    init_step_pin,
    set_dir_pin,
    finish_dir,
    start_step,
    set_step_pin,
    finish_step,
    start_unstep,
    finish_unstep,
    max_pulses_per_sec,
    set_timer_ticks,
    start_timer,
    stop_timer,
    nullptr
};

REGISTER_STEP_ENGINE(I2S, &i2s_engine);
