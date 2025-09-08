// Copyright (c) 2024 -  Mitch Bradley, adjusted by Stefan de Bruijn for S3
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Stepping engine that uses direct GPIO accesses timed by spin loops.

#include "Driver/StepTimer.h"
#include "Driver/delay_usecs.h"
#include "Driver/fluidnc_gpio.h"
#include "Driver/i2s_out.h"
#include "Driver/step_engine.h"

#include <driver/gpio.h>
#include "hal/gpio_hal.h"
#include <esp_attr.h>  // IRAM_ATTR

static int i2s_out_initialized = 0;

static pinnum_t i2s_out_ws_pin   = 255;
static pinnum_t i2s_out_bck_pin  = 255;
static pinnum_t i2s_out_data_pin = 255;

static uint32_t _pulse_delay_us;
static uint32_t _dir_delay_us;

static int _stepPulseEndTime;

static uint32_t i2s_output_ = 0;
static uint32_t i2s_pulse_  = 0;

static gpio_dev_t* _gpio_dev = GPIO_HAL_GET_HW(GPIO_PORT_0);

static int IRAM_ATTR i2s_out_gpio_shiftout(uint32_t port_data) {
    // Only works if gpio num < 32
    volatile uint32_t* out = &(_gpio_dev->out);

    const uint32_t wsbit   = 1 << i2s_out_ws_pin;
    const uint32_t databit = 1 << i2s_out_data_pin;
    const uint32_t clkbit  = 1 << i2s_out_bck_pin;

    // Pre-calculate the possible bit loop values for the out register
    const uint32_t clk0data0 = *out & ~clkbit & ~databit & ~wsbit;
    const uint32_t clk1data0 = clk0data0 | clkbit;
    const uint32_t clk0data1 = clk0data0 | databit;
    const uint32_t clk1data1 = clk1data0 | databit;

    // With int32_t, the high bit can be tested with <0
    int32_t data = port_data;

    // It is not necessary to drive WS low before sending the bits
    // The WS transition that matters is low-to-high, which happens
    // after all the bits are send.  The high-to-low transition is
    // concurrent with the clk-low phase of the first data bit.
    //    *out = clk0data0;

    for (int i = 32; i--;) {
        if (data < 0) {
            *out = clk0data1;  // Establish data 1 during clk low phase
            *out = clk1data1;  // Hold data 1 across low-to-high transition
        } else {
            *out = clk0data0;  // Establish data 0 during clk low phase
            *out = clk1data0;  // Hold data 0 across low-to-high transition
        }
        data >>= 1;
    }
    *out = clk0data0 | wsbit;  // Drive WS high to push to the output register
    return 0;
}

void IRAM_ATTR i2s_out_write(pinnum_t pin, uint8_t val) {
    uint32_t bit = 1 << pin;
    if (val) {
        i2s_output_ |= bit;
    } else {
        i2s_output_ &= ~bit;
    }

    i2s_out_gpio_shiftout(i2s_output_);
}

void IRAM_ATTR i2s_out_delay() {}

uint8_t IRAM_ATTR i2s_out_read(pinnum_t pin) {
    uint32_t port_data = (i2s_output_ ^ i2s_pulse_);
    return !!(port_data & (1 << pin));
}

int i2s_out_init(i2s_out_init_t* init_param) {
    if (i2s_out_initialized) {
        return -1;
    }

    i2s_out_ws_pin   = init_param->ws_pin;
    i2s_out_bck_pin  = init_param->bck_pin;
    i2s_out_data_pin = init_param->data_pin;

    // Set output on the ws, bck and data pins:
    gpio_mode(init_param->ws_pin, 0, 1, 0, 0, 0);
    gpio_mode(init_param->bck_pin, 0, 1, 0, 0, 0);
    gpio_mode(init_param->data_pin, 0, 1, 0, 0, 0);

    i2s_out_gpio_shiftout(init_param->init_val);
    i2s_output_ = init_param->init_val;

    return 0;
}

static uint32_t init_engine(uint32_t dir_delay_us, uint32_t pulse_delay_us, uint32_t frequency, bool (*callback)(void)) {
    stepTimerInit(frequency, callback);
    _dir_delay_us   = dir_delay_us;
    _pulse_delay_us = pulse_delay_us;
    return _pulse_delay_us;
}

static int init_step_pin(int step_pin, int step_invert) {
    return step_pin;
}

static void IRAM_ATTR set_pin(int pin, int level) {
    if (level) {
        i2s_output_ |= (1 << pin);
    } else {
        i2s_output_ &= ~(1 << pin);
    }
    i2s_out_gpio_shiftout(i2s_output_);
}

static void IRAM_ATTR set_step_pin(int pin, int level) {
    if (level) {
        i2s_pulse_ |= (1 << pin);
    } else {
        i2s_pulse_ &= ~(1 << pin);
    }
}

static void IRAM_ATTR finish_dir() {
    delay_us(_dir_delay_us);
}

static void IRAM_ATTR start_step() {
    i2s_pulse_ = 0;
}

// Instead of waiting here for the step end time, we mark when the
// step pulse should end, then return.  The stepper code can then do
// some work that is overlapped with the pulse time.  The spin loop
// will happen in start_unstep()
static void IRAM_ATTR finish_step() {
    _stepPulseEndTime = usToEndTicks(_pulse_delay_us);

    i2s_out_gpio_shiftout(i2s_output_ ^ i2s_pulse_);
}

static int IRAM_ATTR start_unstep() {
    spinUntil(_stepPulseEndTime);
    i2s_out_gpio_shiftout(i2s_output_);
    i2s_pulse_ = 0;
    return 0;
}

// This is a noop because each gpio_write() takes effect immediately,
// so there is no need to commit multiple GPIO changes.
static void IRAM_ATTR finish_unstep() {}

static uint32_t max_pulses_per_sec() {
    const uint32_t max_pps = 1000000 / (2 * _pulse_delay_us);

    const uint32_t hw_max_pps = 80000;
    // 80000 is empirically determined.  The maximum clock rate for
    // ESP32-S3 GPIO bit-banging (without using dedicated GPIOs) is
    // 8 Mhz, so a 32-bit shift-in takes 32/8 = 4 uS.  A step pulse
    // has both a leading and trailing edge, so 8 uS or 125000 pulses
    // per second neglecting software overhead.  I measured 720 ns
    // of software overhead between the two edges and 3280 ns between
    // successive pulses, for a total pulse-to-pulse time of 12 us,
    // so 83333 pulses per second.

    if (max_pps > hw_max_pps) {
        return hw_max_pps;
    }
    return max_pps;
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
    "I2S",
    init_engine,
    init_step_pin,
    set_pin,
    finish_dir,
    start_step,
    set_step_pin,
    finish_step,
    start_unstep,
    finish_unstep,
    max_pulses_per_sec,
    set_timer_ticks,
    start_timer,
    stop_timer
};

REGISTER_STEP_ENGINE(I2S, &engine);
