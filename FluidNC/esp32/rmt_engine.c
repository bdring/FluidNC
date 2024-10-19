// Copyright (c) 2024 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Stepping engine that uses the ESP32 RMT hardware to time step pulses, thus avoiding
// the need to wait for the end of step pulses.

#include "Driver/step_engine.h"
#include "Driver/fluidnc_gpio.h"
#include "Driver/StepTimer.h"
#include <driver/rmt.h>
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

// Allocate an RMT channel and attach the step_pin GPIO to it,
// setting the timing according to dir_delay_us and pulse_delay_us.
// Return the index of that RMT channel which will be presented to
// set_step_pin() later.
static int init_step_pin(int step_pin, int step_inverted) {
    static rmt_channel_t next_RMT_chan_num = RMT_CHANNEL_0;
    if (next_RMT_chan_num == RMT_CHANNEL_MAX) {
        return -1;
    }
    rmt_channel_t rmt_chan_num = next_RMT_chan_num;
    next_RMT_chan_num          = (rmt_channel_t)((int)(next_RMT_chan_num) + 1);

    rmt_config_t rmtConfig = { .rmt_mode      = RMT_MODE_TX,
                               .channel       = rmt_chan_num,
                               .gpio_num      = (gpio_num_t)step_pin,
                               .clk_div       = 20,
                               .mem_block_num = 2,
                               .flags         = 0,
                               .tx_config     = {
                                       .carrier_freq_hz      = 0,
                                       .carrier_level        = RMT_CARRIER_LEVEL_LOW,
                                       .idle_level           = step_inverted ? RMT_IDLE_LEVEL_HIGH : RMT_IDLE_LEVEL_LOW,
                                       .carrier_duty_percent = 50,
#if SOC_RMT_SUPPORT_TX_LOOP_COUNT
                                   .loop_count = 1,
#endif
                                   .carrier_en     = false,
                                   .loop_en        = false,
                                   .idle_output_en = true,
                               } };

    rmt_item32_t rmtItem[2];
    rmtItem[0].duration0 = _dir_delay_us ? _dir_delay_us * 4 : 1;
    rmtItem[0].duration1 = _pulse_delay_us * 4;
    rmtItem[1].duration0 = 0;
    rmtItem[1].duration1 = 0;

    rmtItem[0].level0 = rmtConfig.tx_config.idle_level;
    rmtItem[0].level1 = !rmtConfig.tx_config.idle_level;
    rmt_config(&rmtConfig);
    rmt_fill_tx_items(rmtConfig.channel, &rmtItem[0], rmtConfig.mem_block_num, 0);
    return (int)rmt_chan_num;
}

// The direction pin is a GPIO that is accessed in the usual way
static IRAM_ATTR void set_dir_pin(int pin, int level) {
    gpio_write(pin, level);
}

// The direction delay is handled by the RMT pulser
static IRAM_ATTR void finish_dir() {}

// No need for any common setup before setting step pins
static IRAM_ATTR void start_step() {}

// Restart the RMT which has already been configured
// for the desired pulse length, polarity, and direction delay
static IRAM_ATTR void set_step_pin(int pin, int level) {
#ifdef CONFIG_IDF_TARGET_ESP32
    RMT.conf_ch[pin].conf1.mem_rd_rst = 1;
    RMT.conf_ch[pin].conf1.mem_rd_rst = 0;
    RMT.conf_ch[pin].conf1.tx_start   = 1;
#endif
#ifdef CONFIG_IDF_TARGET_ESP32S3
    RMT.chnconf0[pin].mem_rd_rst_n = 1;
    RMT.chnconf0[pin].mem_rd_rst_n = 0;
    RMT.chnconf0[pin].tx_start_n   = 1;
#endif
}

// This is a noop because the RMT channels do everything
static IRAM_ATTR void finish_step() {}

// This is a noop because the RMT channels take care
// of the pulse trailing edges.
// Return 1 (true) to tell Stepping.cpp that it can
// skip the rest of the step pin deassertion process
static IRAM_ATTR int start_unstep() {
    return 1;
}

// This is a noop and will not be called because start_unstep()
// returns 1
static IRAM_ATTR void finish_unstep() {}

// Possible speedup: If the direction delay were done explicitly
// instead of baking it into the RMT timing, we might be able to
// get more pulses per second, since direction changes are infrequent
// and thus do not need to be applied to every pulse
static uint32_t max_pulses_per_sec() {
    uint32_t pps = 1000000 / (2 * _pulse_delay_us + _dir_delay_us);
    return pps;
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
    "RMT",
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
    stop_timer
};

REGISTER_STEP_ENGINE(RMT, &engine);
