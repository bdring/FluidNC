// Copyright (c) 2024 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Stepping engine that uses the ESP32 RMT hardware to time step pulses, thus avoiding
// the need to wait for the end of step pulses.
//
// **implementation brief**:
// - Uses RMT internal SRAM (not DMA)
// - Pre-fills 2 RMT items (1 pulse + 1 terminator) at init time
// - Runtime just triggers RMT with direct register access (reset pointer + start)
// - No driver API, no dynamic buffers - simple, fast, reliable

#include "Driver/step_engine.h"
#include "Driver/fluidnc_gpio.h"
#include "Driver/StepTimer.h"
#include "Platform.h"
#include <driver/rmt.h>
#include <esp32-hal-gpio.h>
#include <esp_attr.h>  // IRAM_ATTR

// Include RMT hardware structures for direct register access
#ifdef CONFIG_IDF_TARGET_ESP32
    #include <soc/rmt_struct.h>
#endif
#ifdef CONFIG_IDF_TARGET_ESP32S3
    #include <soc/rmt_struct.h>
    #include <hal/rmt_ll.h>  // Low-level RMT API for S3
#endif

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
//
// **initialization brief**:
// - clk_div = 20: APB clock (80MHz) / 20 = 4MHz → 0.25us per RMT tick
// - mem_block_num = 1: Use 1 memory block (64 items, we only need 2)
// - Fill 2 items: [0] = pulse pattern, [1] = terminator (all zeros)
// - The pulse pattern stays in RMT internal SRAM permanently
// - Every trigger replays the same pattern - no runtime updates needed
static uint32_t init_step_pin(pinnum_t step_pin, bool step_inverted) {
    static rmt_channel_t next_RMT_chan_num = RMT_CHANNEL_0;
    if (next_RMT_chan_num >= MAX_N_RMT) {
        return -1;
    }
    rmt_channel_t rmt_chan_num = next_RMT_chan_num;
    next_RMT_chan_num          = (rmt_channel_t)((int)(next_RMT_chan_num) + 1);

    rmt_config_t rmtConfig = { 
        .rmt_mode      = RMT_MODE_TX,
        .channel       = rmt_chan_num,
        .gpio_num      = (gpio_num_t)step_pin,
        .clk_div       = 20,  // 4MHz RMT clock (0.25us per tick)
        .mem_block_num = 3,   // 3 memory blocks = 192 RMT items (reduce FIFO reload interrupts)
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
        } 
    };

    // Prepare 2-item pulse pattern
    rmt_item32_t rmtItem[2];
    
    // Item 0: Pulse pattern
    // duration0 = idle time (low level), duration1 = pulse time (high level)
    // With clk_div=20, each tick = 0.25us, so durations are microseconds * 4
    rmtItem[0].duration0 = _dir_delay_us ? (_dir_delay_us * 4) : 4;  // Min 4 ticks = 1us
    rmtItem[0].duration1 = _pulse_delay_us * 4;
    rmtItem[0].level0    = rmtConfig.tx_config.idle_level;
    rmtItem[0].level1    = !rmtConfig.tx_config.idle_level;
    
    // Item 1: Terminator (all zeros = end of transmission)
    rmtItem[1].duration0 = 0;
    rmtItem[1].duration1 = 0;

    // Configure RMT hardware
    rmt_config(&rmtConfig);
    
    // Copy pulse pattern to RMT internal SRAM (stays there permanently)
    rmt_fill_tx_items(rmtConfig.channel, &rmtItem[0], 2, 0);
    
    return (int)rmt_chan_num;
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
//
// **pulse trigger** (ultra-fast, ~20-50ns overhead):
// 1. Reset memory read pointer to position 0
// 2. Start RMT transmission
// 3. RMT hardware reads items from internal SRAM and generates pulse
// 4. CPU returns immediately - pulse generation is 100% hardware
static void IRAM_ATTR set_step_pin(pinnum_t pin, bool level) {
#ifdef CONFIG_IDF_TARGET_ESP32
    // ESP32 classic: Direct register access
    RMT.conf_ch[pin].conf1.mem_rd_rst = 1;
    RMT.conf_ch[pin].conf1.mem_rd_rst = 0;
    RMT.conf_ch[pin].conf1.tx_start   = 1;
#endif

#ifdef CONFIG_IDF_TARGET_ESP32S3
    // ESP32-S3: Use low-level API (cleaner, forward-compatible)
    rmt_ll_tx_reset_pointer(&RMT, (rmt_channel_t)pin);
    rmt_ll_tx_start(&RMT, (rmt_channel_t)pin);
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

// Maximum pulses per second based on configured pulse timing
//
// With 4MHz RMT clock (clk_div=20):
// - Theoretical max depends on pulse width + dir delay
// - Example: 5us pulse + 2us delay = 7us total → ~143kHz
// - ESP32-S3 should achieve similar or better performance
// - Higher clock precision (0.25us vs 1us) reduces quantization jitter
//
// Note: Actual max rate also depends on:
// - ISR execution time (~1-2us per interrupt)
// - Stepper trajectory calculation overhead
// - Number of axes moving simultaneously
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
