// Copyright (c) 2024 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Stepping engine that uses the ESP32 RMT hardware to time step pulses, thus avoiding
// the need to wait for the end of step pulses.

#include "Driver/step_engine.h"
#include "Driver/fluidnc_gpio.h"
#include "Driver/StepTimer.h"
#include "Config.h"  // Assert
// The RMT channels are set up with the rmt_ll_* HAL calls instead of a driver.
// The legacy driver (driver/rmt.h) is gone in IDF 6, and the new one hides the
// channel number that set_step_pin() needs to restart a channel from the
// stepping ISR.  The setup sequence mirrors IDF 5's legacy rmt_config() and
// rmt_fill_tx_items().
#include <esp_idf_version.h>
#include <esp_private/periph_ctrl.h>  // PERIPH_RCC_ATOMIC
#include <soc/rmt_struct.h>           // RMT
#include <soc/soc_caps.h>             // SOC_RMT_MEM_WORDS_PER_CHANNEL
#include <hal/rmt_ll.h>
#if ESP_IDF_VERSION_MAJOR >= 6
#    include <hal/rmt_periph.h>
#    define RMT_TX_CHANNELS RMT_LL_TX_CANDIDATES_PER_INST
#    define RMT_TX_SIGNAL(chan) soc_rmt_signals[0].channels[chan].tx_sig
#else
#    include <soc/rmt_periph.h>
#    define RMT_TX_CHANNELS SOC_RMT_TX_CANDIDATES_PER_GROUP
#    define RMT_TX_SIGNAL(chan) rmt_periph_signals.groups[0].channels[chan].tx_sig
#endif
#include <esp_attr.h>  // IRAM_ATTR

static uint32_t _pulse_delay_us;
static uint32_t _dir_delay_us;

static uint32_t init_engine(uint32_t dir_delay_us, uint32_t pulse_delay_us, uint32_t& frequency, bool (*callback)(void)) {
    frequency       = stepTimerInit(callback);
    _dir_delay_us   = dir_delay_us;
    _pulse_delay_us = pulse_delay_us;
    return _pulse_delay_us;
}

// Allocate an RMT channel and attach the step_pin GPIO to it,
// setting the timing according to dir_delay_us and pulse_delay_us.
// Return the index of that RMT channel which will be presented to
// set_step_pin() later.
// RMT channel memory, provided by the target's peripherals.ld.  Channel n's
// block starts at word n * SOC_RMT_MEM_WORDS_PER_CHANNEL.
extern "C" volatile uint32_t RMTMEM[];

// One RMT item: duration0 in bits 0-14, level0 in bit 15, duration1 in
// bits 16-30, level1 in bit 31.  A zero duration ends the transmission.
static uint32_t rmt_item(uint32_t duration0, bool level0, uint32_t duration1, bool level1) {
    return (duration0 & 0x7fff) | (uint32_t(level0) << 15) | ((duration1 & 0x7fff) << 16) | (uint32_t(level1) << 31);
}

static void rmt_enable_module() {
    static bool enabled = false;
    if (enabled) {
        return;
    }
    PERIPH_RCC_ATOMIC() {
        rmt_ll_enable_bus_clock(0, true);
        rmt_ll_reset_register(0);
    }
    rmt_ll_mem_power_by_pmu(&RMT);
    rmt_ll_enable_mem_access_nonfifo(&RMT, true);
    enabled = true;
}

// Allocate an RMT TX channel and attach the step_pin GPIO to it,
// setting the timing according to dir_delay_us and pulse_delay_us.
// Return the index of that RMT channel which will be presented to
// set_step_pin() later.
static uint32_t init_step_pin(pinnum_t step_pin, bool step_inverted) {
    static uint32_t next_chan = 0;
    // Each RMT-stepped motor needs its own TX channel (8 on ESP32, 4 on S3)
    Assert(next_chan < RMT_TX_CHANNELS, "Out of RMT channels for step pins");
    uint32_t chan = next_chan++;

    rmt_enable_module();

    // APB (80 MHz) is always running on ESP32 and ESP32-S3.  The source is
    // per channel on ESP32 and per group on ESP32-S3, so set it each time.
    PERIPH_RCC_ATOMIC() {
        rmt_ll_set_group_clock_src(&RMT, chan, RMT_CLK_SRC_APB, 1, 0, 0);
        rmt_ll_enable_group_clock(&RMT, true);
    }

    // 80 MHz APB / 20 = 4 MHz, so durations are in units of 0.25 us
    bool idle_level = step_inverted;
    rmt_ll_tx_set_channel_clock_div(&RMT, chan, 20);
    // The two items fit in one block, so each channel uses only its own block
    rmt_ll_tx_set_mem_blocks(&RMT, chan, 1);
    rmt_ll_tx_reset_pointer(&RMT, chan);
    rmt_ll_tx_enable_loop(&RMT, chan, false);
    rmt_ll_tx_enable_wrap(&RMT, chan, true);
    rmt_ll_tx_fix_idle_level(&RMT, chan, idle_level, true);
    rmt_ll_tx_enable_carrier_modulation(&RMT, chan, false);
    rmt_ll_tx_set_carrier_level(&RMT, chan, 0);

    // Idle level for the direction delay, then the active level for the
    // pulse, then an end marker.
    volatile uint32_t* mem = &RMTMEM[chan * SOC_RMT_MEM_WORDS_PER_CHANNEL];
    mem[0]                 = rmt_item(_dir_delay_us ? _dir_delay_us * 4 : 1, idle_level, _pulse_delay_us * 4, !idle_level);
    mem[1]                 = 0;

    gpio_route(step_pin, RMT_TX_SIGNAL(chan));

    return chan;
}

// The direction pin is a GPIO that is accessed in the usual way
static void IRAM_ATTR set_dir_pin(pinnum_t pin, bool level) {
    gpio_write(pin, level);
}

// The direction delay is handled by the RMT pulser
static void IRAM_ATTR finish_dir() {}

// No need for any common setup before setting step pins
static void IRAM_ATTR start_step() {}

// Restart the RMT which has already been configured
// for the desired pulse length, polarity, and direction delay
static void IRAM_ATTR set_step_pin(pinnum_t pin, bool level) {
    // Same writes as rmt_ll_tx_reset_pointer() + rmt_ll_tx_start(), minus the
    // APB-side memory pointer reset, which only matters for FIFO access.
#if CONFIG_IDF_TARGET_ESP32
    RMT.conf_ch[pin].conf1.mem_rd_rst = 1;
    RMT.conf_ch[pin].conf1.mem_rd_rst = 0;
    RMT.conf_ch[pin].conf1.tx_start   = 1;
#elif CONFIG_IDF_TARGET_ESP32S3
    RMT.chnconf0[pin].mem_rd_rst_chn  = 1;
    RMT.chnconf0[pin].mem_rd_rst_chn  = 0;
    // Latch the channel configuration, as rmt_ll_tx_start() does
    RMT.chnconf0[pin].conf_update_chn = 1;
    RMT.chnconf0[pin].tx_start_chn    = 1;
#endif
}

// This is a noop because the RMT channels do everything
static void IRAM_ATTR finish_step() {}

// This is a noop because the RMT channels take care
// of the pulse trailing edges.
// Return 1 (true) to tell Stepping.cpp that it can
// skip the rest of the step pin deassertion process
static bool IRAM_ATTR start_unstep() {
    return 1;
}

// This is a noop and will not be called because start_unstep()
// returns 1
static void IRAM_ATTR finish_unstep() {}

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
    stop_timer,
    NULL
};

REGISTER_STEP_ENGINE(RMT, &engine);
