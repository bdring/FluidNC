// Copyright (c) 2026 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Driver/step_engine.h"
#include "Driver/delay_usecs.h"
#include "Driver/fluidnc_gpio.h"
#include "Driver/StepTimer.h"

#include <hal/rmt_ll.h>
#include <driver/periph_ctrl.h>
#include <esp_intr_alloc.h>
#include <esp_attr.h>
#include <soc/gpio_sig_map.h>
#include <soc/rmt_struct.h>
#include <soc/soc.h>

#define RMT_QUEUED_MEM_BLOCKS 1
#define RMT_QUEUED_MEM_WORDS 64
#define RMT_QUEUED_TX_LIMIT 4
#define RMT_CHANNEL_COUNT 8
#define RMT_DIR_CHANGE_QUEUE 16

static uint32_t _pulse_delay_us;
static uint32_t _pulse_ticks;
static uint32_t _dir_delay_us;
static uint32_t _timer_frequency_hz;
static uint32_t _next_interval_us;
static uint32_t _next_interval_ticks;
static uint32_t _next_delay_ticks;

static bool _rmt_intr_installed = false;
static bool _rmt_initialized    = false;
static bool _dir_transition     = false;
static bool _pulse_idle         = false;
static bool (*_pulse_func)(void) = nullptr;

static uint64_t _pending_dir_changed = 0;
static uint64_t _pending_dir_values = 0;

static void record_pending_dir_change(pinnum_t pin, bool level) {
    uint32_t pin_index = (uint32_t)pin;
    if (pin_index < 64) {
        uint64_t bit = 1ull << pin_index;
        _pending_dir_changed |= bit;
        if (level) {
            _pending_dir_values |= bit;
        } else {
            _pending_dir_values &= ~bit;
        }
    }
}

struct queued_channel_t {
    bool     active;
    bool     started;
    pinnum_t gpio_pin;
    bool     step_inverted;
    uint32_t write_index;
    bool     step_pending;
    uint32_t idle_level;
    uint32_t active_level;
};

#if V5_1
#    define INT_STAT_REG rmt_ll_get_interrupt_status_reg(&RMT)
#else
#    define INT_STAT_REG &(RMT.int_st)

#    define rmt_symbol_word_t rmt_item32_t
#endif
static queued_channel_t _channels[RMT_CHANNEL_COUNT];
static uint32_t _next_rmt_channel = 0;
static const uint32_t _rmt_master_channel = 0;

static inline uint32_t ticks_from_us(uint32_t usecs) {
    uint32_t ticks = usecs * 4;
    return ticks ? ticks : 1;
}

static inline rmt_symbol_word_t make_symbol(uint32_t channel, bool step, uint32_t delay_ticks = UINT32_MAX) {
    auto& ch = _channels[channel];

    if (delay_ticks == UINT32_MAX) {
        delay_ticks = _next_delay_ticks;
    }

    rmt_symbol_word_t symbol = {};
    symbol.duration0 = _pulse_ticks ? _pulse_ticks : 1;
    symbol.duration1 = delay_ticks;
    symbol.level0 = (uint16_t)(step ? ch.active_level : ch.idle_level);
    symbol.level1 = (uint16_t)ch.idle_level;
    return symbol;
}

static inline void write_symbol(uint32_t channel, uint32_t index, const rmt_symbol_word_t& symbol) {
    RMTMEM.chan[channel].data32[index].val = symbol.val;
}

static inline void write_eof_symbol(uint32_t channel, uint32_t index) {
    rmt_item32_t symbol = {};
    write_symbol(channel, index, symbol);
}

static inline void rmt_queued_schedule_next_symbol(uint32_t channel) {
    auto&    ch         = _channels[channel];
    uint32_t next_index = (ch.write_index + 1) % RMT_QUEUED_MEM_WORDS;

    if (ch.step_pending) {
        ch.step_pending = false;
        write_symbol(channel, ch.write_index, make_symbol(channel, true));
    } else {
        write_symbol(channel, ch.write_index, make_symbol(channel, false));
    }

    write_eof_symbol(channel, next_index);
    ch.write_index = next_index;
}

static void rmt_enable_thres_interrupts(bool enable) {
    if (_next_rmt_channel == 0 || !_channels[_rmt_master_channel].active) {
        return;
    }
#if V5_1
    rmt_ll_enable_interrupt(&RMT, RMT_LL_EVENT_TX_THRES(_rmt_master_channel), enable);
#else
    // V4 has rmt_ll_enable_interrupt but not RMT_LL_EVENT*
    rmt_ll_enable_tx_thres_interrupt(&RMT, _rmt_master_channel, enable);
#endif
}

static void rmt_enable_done_interrupts(bool enable) {
    if (_next_rmt_channel == 0 || !_channels[_rmt_master_channel].active) {
        return;
    }
#if V5_1
    rmt_ll_enable_interrupt(&RMT, RMT_LL_EVENT_TX_DONE(_rmt_master_channel), enable);
#else
    // V4 has rmt_ll_enable_interrupt but not RMT_LL_EVENT*
    rmt_ll_enable_tx_end_interrupt(&RMT, _rmt_master_channel, enable);
#endif
}

static void rmt_pause_for_dir_change() {
    if (_dir_transition) {
        return;
    }
    _dir_transition = true;
    rmt_enable_thres_interrupts(false);
    rmt_enable_done_interrupts(true);
    for (uint32_t channel = 0; channel < _next_rmt_channel; ++channel) {
        auto& ch = _channels[channel];
        if (ch.active) {
            write_eof_symbol(channel, ch.write_index);
        }
    }
}

static void rmt_queued_resume_channel(uint32_t channel) {
    auto& ch = _channels[channel];
    ch.write_index = 0;
    rmt_ll_tx_reset_pointer(&RMT, channel);
    rmt_queued_schedule_next_symbol(channel);
    ch.started = false;
}

static void rmt_apply_pending_dir_changes() {
    uint64_t pending = _pending_dir_changed;
    while (pending) {
        uint32_t pin = __builtin_ctzll(pending);
        bool level = (_pending_dir_values >> pin) & 1u;
        gpio_write((pinnum_t)pin, level);
        pending &= pending - 1;
    }
    _pending_dir_changed = 0;
    _pending_dir_values = 0;
}

static void start_all_channels() {
    if (_dir_transition) {
        return;
    }

    for (uint32_t channel = 0; channel < _next_rmt_channel; ++channel) {
        auto& ch = _channels[channel];
        if (!ch.active || ch.started) {
            continue;
        }
        ch.started = true;
        rmt_ll_tx_start(&RMT, channel);
    }
}

static void IRAM_ATTR rmt_engine_isr(void* arg) {
    (void)arg;
    uint32_t status = rmt_ll_get_tx_thres_interrupt_status(&RMT);
    if (status) {
#if V5_1
        rmt_ll_clear_interrupt_status(&RMT, RMT_LL_EVENT_TX_THRES(_rmt_master_channel));
#else
        rmt_ll_clear_tx_thres_interrupt(&RMT, _rmt_master_channel);
#endif
        if (_pulse_func) {
            _pulse_idle = !_pulse_func();
        }
    }

    uint32_t done_status = rmt_ll_get_tx_end_interrupt_status(&RMT);
    if (done_status) {
        while (done_status) {
            uint32_t channel = __builtin_ctz(done_status);
#if V5_1
            rmt_ll_clear_interrupt_status(&RMT, RMT_LL_EVENT_TX_DONE(channel));
#else
            rmt_ll_clear_tx_end_interrupt(&RMT, channel);
#endif
            done_status &= done_status - 1;
        }

        if (_dir_transition) {
            rmt_apply_pending_dir_changes();
            if (_dir_delay_us) {
                delay_us(_dir_delay_us);
            }
            rmt_enable_thres_interrupts(true);
            _dir_transition = false;
        }

        if (!_pulse_idle) {
            for (uint32_t channel = 0; channel < _next_rmt_channel; ++channel) {
                if (_channels[channel].active) {
                    rmt_queued_resume_channel(channel);
                }
            }
            start_all_channels();
        }
    }
}

static void ensure_rmt_interrupt() {
    if (_rmt_intr_installed) {
        return;
    }

    uint32_t mask = 0;
    for (uint32_t channel = 0; channel < RMT_CHANNEL_COUNT; ++channel) {
        mask |= (1u << (channel + 24));
    }

    esp_intr_alloc_intrstatus(
        ETS_RMT_INTR_SOURCE, ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3, (uint32_t)INT_STAT_REG, mask, rmt_engine_isr, NULL, NULL);
    _rmt_intr_installed = true;
}

static uint32_t init_engine(uint32_t dir_delay_us, uint32_t pulse_delay_us, uint32_t& frequency, bool (*callback)(void)) {
    if (!_rmt_initialized) {
        periph_module_enable(PERIPH_RMT_MODULE);
#if V5_1
        rmt_ll_enable_mem_access_nonfifo(&RMT, true);
#else
        rmt_ll_enable_mem_access(&RMT, true);
#endif
        _rmt_initialized = true;
    }

    _pulse_func          = callback;
    _timer_frequency_hz  = frequency;
    _dir_delay_us        = dir_delay_us;
    _pulse_delay_us      = pulse_delay_us;
    _pulse_ticks         = ticks_from_us(_pulse_delay_us);
    _next_interval_us    = 0;
    _next_interval_ticks = _pulse_ticks;
    _next_delay_ticks    = 0;

    ensure_rmt_interrupt();

    return _pulse_delay_us;
}

static uint32_t init_step_pin(pinnum_t step_pin, bool step_inverted) {
    if (_next_rmt_channel >= RMT_CHANNEL_COUNT) {
        return (uint32_t)-1;
    }

    uint32_t channel = _next_rmt_channel++;
    auto&    ch      = _channels[channel];

    ch.active        = true;
    ch.started       = false;
    ch.gpio_pin      = step_pin;
    ch.step_inverted = step_inverted;
    ch.write_index   = 0;
    ch.step_pending  = false;
    ch.idle_level    = step_inverted ? 1 : 0;
    ch.active_level  = !ch.idle_level;

    // Configure RMT channel hardware for direct memory access.
    rmt_ll_tx_set_channel_clock_div(&RMT, channel, 20);
    rmt_ll_tx_set_mem_blocks(&RMT, channel, RMT_QUEUED_MEM_BLOCKS);

#if V5_1
    rmt_ll_tx_fix_idle_level(&RMT, channel, ch.idle_level, true);
    rmt_ll_tx_enable_wrap(&RMT, channel, true);
#else
    // V5 rmt_ll_tx_fix_idle_level() is equivalent to the following
    rmt_ll_tx_enable_idle(&RMT, channel, true);
    rmt_ll_tx_set_idle_level(&RMT, channel, ch.idle_level);
    rmt_ll_tx_enable_pingpong(&RMT, channel, true);
#endif

    rmt_ll_tx_set_limit(&RMT, channel, RMT_QUEUED_TX_LIMIT);
    if (channel == _rmt_master_channel) {
#if V5_1
        rmt_ll_enable_interrupt(&RMT, RMT_LL_EVENT_TX_THRES(channel), true);
        rmt_ll_enable_interrupt(&RMT, RMT_LL_EVENT_TX_DONE(channel), true);
#else
        // V4 has rmt_ll_enable_interrupt but not RMT_LL_EVENT*
        rmt_ll_enable_tx_thres_interrupt(&RMT, channel, true);
        rmt_ll_enable_tx_end_interrupt(&RMT, channel, true);
#endif
    }
    rmt_ll_tx_reset_pointer(&RMT, channel);

#if !V5_1
    // The V5 rmt_ll_tx_reset_pointer() includes this but V4 omits it
    RMT.conf_ch[channel].conf1.apb_mem_rst = 1;
    RMT.conf_ch[channel].conf1.apb_mem_rst = 0;
#endif

    write_eof_symbol(channel, 0);

    gpio_route(step_pin, RMT_SIG_OUT0_IDX + channel);

    return channel;
}

static void IRAM_ATTR set_dir_pin(pinnum_t pin, bool level) {
    record_pending_dir_change(pin, level);
}

static void IRAM_ATTR finish_dir() {
}

static void IRAM_ATTR start_step() {}

static void IRAM_ATTR set_step_pin(pinnum_t channel, bool level) {
    if (!level) {
        return;
    }

    if ((uint32_t)channel >= _next_rmt_channel) {
        return;
    }

    auto& ch = _channels[channel];
    ch.step_pending = true;
}

static void IRAM_ATTR finish_step() {
    if (_dir_transition) {
        return;
    }

    _pulse_idle = false;
    if (_pending_dir_changed) {
        rmt_pause_for_dir_change();
    } else {
        for (uint32_t channel = 0; channel < _next_rmt_channel; ++channel) {
            auto& ch = _channels[channel];
            if (!ch.active) {
                continue;
            }
            rmt_queued_schedule_next_symbol(channel);
        }
        start_all_channels();
    }
}

static bool IRAM_ATTR start_unstep() {
    return true;
}

static void IRAM_ATTR finish_unstep() {}

static uint32_t max_pulses_per_sec() {
    uint32_t pps = 1000000 / (2 * _pulse_delay_us + _dir_delay_us);
    return pps;
}

static void IRAM_ATTR set_timer_ticks(uint32_t ticks) {
    if (_timer_frequency_hz && ticks) {
        uint64_t usecs = (uint64_t)ticks * 1000000ULL;
        _next_interval_us = (uint32_t)((usecs + _timer_frequency_hz - 1) / _timer_frequency_hz);
        _next_interval_ticks = ticks_from_us(_next_interval_us);
        _next_delay_ticks = _next_interval_ticks > _pulse_ticks ? _next_interval_ticks - _pulse_ticks : 0;
    }
}

static void IRAM_ATTR start_timer() {
    // The RMT queued engine is driven by the RMT threshold ISR, not by the step timer.
}

static void IRAM_ATTR stop_timer() {}

static step_engine_t engine = {
    "RMT_Queued",
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

REGISTER_STEP_ENGINE(RMT_Queued, &engine);
