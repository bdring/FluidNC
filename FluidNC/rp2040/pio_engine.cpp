// Copyright (c) 2024 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// RP2040 PIO-based stepping engine.
// Uses a PIO state machine to drive step pins from a 32-bit shadow register.

#include "Driver/step_engine.h"
#include "Driver/fluidnc_gpio.h"
#include "Driver/delay_usecs.h"
#include "StepTimer.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"

static uint32_t _pulse_delay_us;
static uint32_t _dir_delay_us;
static int32_t  _stepPulseEndTime;

static PIO      _pio         = pio0;
static uint     _sm          = 0;
static uint     _offset      = 0;
static bool     _use_pio     = false;
static uint32_t _step_shadow = 0;
static uint32_t _step_mask   = 0;

static uint16_t _stepper_prog_instr[3];
static bool     _program_encoded = false;

static const pio_program_t _stepper_program = {
    .instructions = _stepper_prog_instr,
    .length       = 3,
    .origin       = -1,
};

static inline void commit_step_shadow() {
    if (!_use_pio) {
        return;
    }
    while (pio_sm_is_tx_fifo_full(_pio, _sm)) {
    }
    pio_sm_put(_pio, _sm, _step_shadow);
}

static uint32_t init_engine(uint32_t dir_delay_us, uint32_t pulse_delay_us, uint32_t& frequency, bool (*callback)(void)) {
    (void)frequency;

    _use_pio     = false;
    _step_mask   = 0;
    _step_shadow = 0;

    if (!_program_encoded) {
        _stepper_prog_instr[0] = pio_encode_pull(false, true);
        _stepper_prog_instr[1] = pio_encode_out(pio_pins, 32);
        _stepper_prog_instr[2] = pio_encode_jmp(0);
        _program_encoded       = true;
    }

    int sm = pio_claim_unused_sm(_pio, false);
    if (sm >= 0 && pio_can_add_program(_pio, &_stepper_program)) {
        _sm     = (uint)sm;
        _offset = pio_add_program(_pio, &_stepper_program);

        pio_sm_config cfg = pio_get_default_sm_config();
        sm_config_set_out_pins(&cfg, 0, 32);
        sm_config_set_out_shift(&cfg, true, false, 32);
        sm_config_set_clkdiv(&cfg, 1.0f);
        pio_sm_init(_pio, _sm, _offset, &cfg);
        pio_sm_set_enabled(_pio, _sm, true);
        _use_pio = true;
    }

    stepTimerAttach(callback);
    _dir_delay_us   = dir_delay_us;
    _pulse_delay_us = pulse_delay_us;
    return _pulse_delay_us;
}

static uint32_t init_step_pin(pinnum_t step_pin, bool step_invert) {
    if (_use_pio) {
        pio_gpio_init(_pio, (uint)step_pin);
        pio_sm_set_consecutive_pindirs(_pio, _sm, (uint)step_pin, 1, true);
        _step_mask |= (1u << step_pin);
        if (step_invert) {
            _step_shadow |= (1u << step_pin);
        } else {
            _step_shadow &= ~(1u << step_pin);
        }
        commit_step_shadow();
    }
    return step_pin;
}

static void set_dir_pin(pinnum_t pin, bool level) {
    gpio_write(pin, level);
}

static void set_step_pin(pinnum_t pin, bool level) {
    if (_use_pio && ((_step_mask & (1u << pin)) != 0)) {
        if (level) {
            _step_shadow |= (1u << pin);
        } else {
            _step_shadow &= ~(1u << pin);
        }
        return;
    }
    gpio_write(pin, level);
}

static void finish_dir() {
    delay_us(_dir_delay_us);
}

static void start_step() {}

static void finish_step() {
    commit_step_shadow();
    _stepPulseEndTime = usToEndTicks(_pulse_delay_us);
}

static bool start_unstep() {
    spinUntil(_stepPulseEndTime);
    return false;
}

static void finish_unstep() {
    commit_step_shadow();
}

static uint32_t max_pulses_per_sec() {
    return 1000000 / (2 * _pulse_delay_us);
}

static void set_timer_ticks(uint32_t ticks) {
    stepTimerSetTicks(ticks);
}

static void start_timer() {
    stepTimerStart();
}

static void stop_timer() {
    stepTimerStop();
}

static step_engine_t engine = {
    "PIO",
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

REGISTER_STEP_ENGINE(PIO, &engine);