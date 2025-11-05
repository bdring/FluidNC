// Copyright (c) 2024 -  Mitch Bradley, adjusted by Stefan de Bruijn for S3
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// I2S stepping engine that uses ESP32-S3 "dedicated GPIO" accesses timed by spin loops.
// It can achieve 150 kHz pulse rates with the I2S BCK at 21.5 Mhz
// Higher BCK rates are possible, but do not increase the pulse rate much due to
// software overhead elsewhere in the system, and run the risk of exceeding shift
// register max clock rates.

#include "Driver/StepTimer.h"
#include "Driver/delay_usecs.h"
#include "Driver/fluidnc_gpio.h"
#include "Driver/i2s_out.h"
#include "Driver/step_engine.h"

#include <driver/gpio.h>
#include "hal/gpio_hal.h"
#include <esp_attr.h>  // IRAM_ATTR
#include "driver/dedic_gpio.h"
#include "hal/cpu_ll.h"
#include <esp_idf_version.h>

#if ESP_IDF_VERSION_MAJOR >= 5
// Hmm. We might as well just use asm volatile("ee.wr_mask_gpio_out %0, %1" : : "r"(value), "r"(mask):);
// The API isn't as stable as I would like
#    include "hal/dedic_gpio_cpu_ll.h"
#    define cpu_ll_write_dedic_gpio_mask dedic_gpio_cpu_ll_write_mask
#endif


static bool i2s_out_initialized = 0;

static uint32_t _pulse_delay_us;
static uint32_t _dir_delay_us;

static int32_t _stepPulseEndTime;

static uint32_t i2s_output_ = 0;
static uint32_t i2s_pulse_  = 0;

struct dedic_gpio_bundle_t* bundle = NULL;

static void setup_dedicated_gpios(pinnum_t bck_pin, pinnum_t data_pin, pinnum_t ws_pin) {
    int bundle_gpios[] = { data_pin, bck_pin, ws_pin };

    dedic_gpio_bundle_config_t bundle_config = {
        .gpio_array = bundle_gpios,
        .array_size = sizeof(bundle_gpios) / sizeof(bundle_gpios[0]),
        .flags = {
            .out_en = 1,
        },
    };
    ESP_ERROR_CHECK(dedic_gpio_new_bundle(&bundle_config, &bundle));
}

static inline __attribute__((always_inline)) void oneclock(int32_t data) {
    cpu_ll_write_dedic_gpio_mask(3, data < 0);  // Set bck to 0 and data to the data bit
    __asm__ __volatile__("nop");                // Delay to reduce bck rate to about 21Mhz
    __asm__ __volatile__("nop");                // to accommodate shift register max frequency
    __asm__ __volatile__("nop");                // and board layout signal limitations
    __asm__ __volatile__("nop");
    cpu_ll_write_dedic_gpio_mask(2, 2);  // Set bck to 1, leaving data as-is
    __asm__ __volatile__("nop");
    __asm__ __volatile__("nop");
    __asm__ __volatile__("nop");
    __asm__ __volatile__("nop");
}

static void IRAM_ATTR i2s_out_gpio_shiftout(uint32_t port_data) {
    // With int32_t, the high bit can be tested with <0
    int32_t data = port_data;

    // It is not necessary to drive WS low before sending the bits
    // The WS transition that matters is low-to-high, which happens
    // after all the bits are send.  The high-to-low transition is
    // concurrent with the clk-low phase of the first data bit.
    //    *out = clk0data0;

    cpu_ll_write_dedic_gpio_mask(4, 0);  // WS 0

    // Unrolled loop to avoid branch overhead
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;
    oneclock(data);
    data <<= 1;

    cpu_ll_write_dedic_gpio_mask(4, 4);  // WS 1
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

void i2s_out_init(i2s_out_init_t* init_param) {
    if (i2s_out_initialized) {
        return;
    }

    if (init_param->ws_drive_strength != -1) {
        gpio_drive_strength(init_param->ws_pin, init_param->ws_drive_strength);
    }
    if (init_param->bck_drive_strength != -1) {
        gpio_drive_strength(init_param->bck_pin, init_param->bck_drive_strength);
    }
    if (init_param->data_drive_strength != -1) {
        gpio_drive_strength(init_param->data_pin, init_param->data_drive_strength);
    }

    setup_dedicated_gpios(init_param->bck_pin, init_param->data_pin, init_param->ws_pin);

    i2s_out_gpio_shiftout(init_param->init_val);
    i2s_output_ = init_param->init_val;
}

static uint32_t init_engine(uint32_t dir_delay_us, uint32_t pulse_delay_us, uint32_t frequency, bool (*callback)(void)) {
    stepTimerInit(frequency, callback);
    _dir_delay_us   = dir_delay_us;
    _pulse_delay_us = pulse_delay_us;
    return _pulse_delay_us;
}

static uint32_t init_step_pin(pinnum_t step_pin, bool step_invert) {
    return step_pin;
}

static void IRAM_ATTR set_pin(pinnum_t pin, bool level) {
    if (level) {
        i2s_output_ |= (uint32_t)(1u << (int)pin);
    } else {
        i2s_output_ &= (uint32_t)(~(1u << (int)pin));
    }
    i2s_out_gpio_shiftout(i2s_output_);
}

static void IRAM_ATTR set_step_pin(pinnum_t pin, bool level) {
    if (level) {
        i2s_pulse_ |= (uint32_t)(1u << (int)pin);
    } else {
        i2s_pulse_ &= (uint32_t)(~(1u << (int)pin));
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

static bool IRAM_ATTR start_unstep() {
    spinUntil(_stepPulseEndTime);
    i2s_out_gpio_shiftout(i2s_output_);
    i2s_pulse_ = 0;
    return false;
}

// This is a noop because each gpio_write() takes effect immediately,
// so there is no need to commit multiple GPIO changes.
static void IRAM_ATTR finish_unstep() {}

static uint32_t max_pulses_per_sec() {
    const uint32_t max_pps = 1000000 / (2 * _pulse_delay_us);

    // The following value is empirically determined, mostly limited
    // by inter-pulse software overhead.
    const uint32_t hw_max_pps = 150000;

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
    stop_timer,
    NULL
};

REGISTER_STEP_ENGINE(I2S, &engine);
