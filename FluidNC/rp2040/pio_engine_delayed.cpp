// Copyright (c) 2024 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// RP2040 PIO-based stepping engine.
// Uses a PIO state machine to drive step pins from a 32-bit shadow register.

#include "Driver/step_engine.h"
#include "Driver/fluidnc_gpio.h"
#include "Driver/delay_usecs.h"
#include "StepTimer.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/pio_instructions.h"

static uint32_t _pulse_delay_us;
static uint32_t _dir_delay_us;
static uint32_t _delay_counts = 40;
static uint32_t _tick_divisor;

static bool _pio_inited = false;
static PIO  _pio        = pio0;
static uint _sm         = 0;
static uint _irq_index;  // 0 or 1, which PIO IRQ line we're using

static uint32_t _unstep_bits = 0;
static uint32_t _step_bits   = 0;

static uint8_t _min_step_pin = 32;
static uint8_t _max_step_pin = 0;

int32_t pio_underrun_count;  // Diagnostic

bool (*_pulse_func)();

#define PIO_MHZ 2

#if 0

pio_program_t* generate_stepper_program(uint32_t pulse_us, uint32_t pio_clock_mhz) {
    const int len = 3;

    uint16_t* instructions = (uint16_t*)malloc(len * sizeof(uint16_t));
    instructions[0]        = pio_encode_pull(false, true);
    instructions[1]        = pio_encode_out(pio_pins, 32);
    instructions[2]        = pio_encode_jmp(0);

    pio_program_t* program = (pio_program_t*)malloc(sizeof(pio_program_t));
    program->instructions  = instructions;
    program->length        = len;
    program->origin        = -1;  // Let the SDK dynamically allocate the memory location

    return program;
}
#    define PIO_MHZ 1
#endif

static void __not_in_flash_func(_pio_isr)() {
    // Keep pushing while there's room in the TX FIFO.
    while (!pio_sm_is_tx_fifo_full(_pio, _sm)) {
        if (!_pulse_func()) {
            // No more data right now — disable this IRQ source so
            // we stop being called until re-armed (e.g. when new
            // pulse data is queued).
            pio_set_irqn_source_enabled(_pio, _irq_index, (pio_interrupt_source_t)(pis_sm0_tx_fifo_not_full + _sm), false);
            break;
        }
    }

    // Check for underrun since we were last here
    uint32_t txstall_mask = 1u << (PIO_FDEBUG_TXSTALL_LSB + _sm);
    if (_pio->fdebug & txstall_mask) {
        pio_underrun_count++;
        _pio->fdebug = txstall_mask;  // clear
    }
}

static void _pio_irq_init() {
    uint pio_idx = pio_get_index(_pio);
    _irq_index   = 0;  // using IRQ line 0 for this PIO block
    uint irq_num = (pio_idx == 0) ? PIO0_IRQ_0 : PIO1_IRQ_0;

    irq_set_exclusive_handler(irq_num, _pio_isr);
    irq_set_enabled(irq_num, true);
}

// Call whenever you queue new pulse data and want the ISR to
// resume asking _pulse_func() for more.
static void _pio_irq_arm() {
    pio_set_irqn_source_enabled(_pio, _irq_index, (pio_interrupt_source_t)(pis_sm0_tx_fifo_not_full + _sm), true);

    // An initial FIFO stall is likely when we first enable the IRQ
    pio_underrun_count--;
}

static uint32_t init_engine(uint32_t dir_delay_us, uint32_t pulse_delay_us, uint32_t& frequency, bool (*callback)(void)) {
    (void)frequency;

    _step_bits   = 0;
    _unstep_bits = 0;

    _min_step_pin = 32;
    _max_step_pin = 0;

    _pulse_func = callback;

    _tick_divisor   = PIO_MHZ;
    _dir_delay_us   = dir_delay_us;
    _pulse_delay_us = pulse_delay_us;
    return _pulse_delay_us;
}
// XXX need to setup the fifo refill irq

static uint32_t init_step_pin(pinnum_t step_pin, bool step_invert) {
    pio_gpio_init(_pio, (uint)step_pin);
    pio_sm_set_consecutive_pindirs(_pio, _sm, (uint)step_pin, 1, true);
    if (step_pin < _min_step_pin) {
        _min_step_pin = step_pin;
    }
    if (step_pin > _max_step_pin) {
        _max_step_pin = step_pin;
    }
    if (step_invert) {
        _unstep_bits |= (1u << step_pin);
    }
    return step_pin;
}

static void set_dir_pin(pinnum_t pin, bool level) {
    gpio_write(pin, level);
}

static void set_step_pin(pinnum_t pin, bool level) {
    if (level) {
        _step_bits |= 1 << (pin - _min_step_pin);
    }
}

static void finish_dir() {
    delay_us(_dir_delay_us);
}

static void start_step() {}

static void finish_step() {
    while (pio_sm_is_tx_fifo_full(_pio, _sm)) {}
    pio_sm_put(_pio, _sm, _step_bits);
    _step_bits = 0;
}

static bool start_unstep() {
    return false;
}

static void finish_unstep() {}

static uint32_t max_pulses_per_sec() {
    return 1000000 / (2 * _pulse_delay_us);
}

static void set_timer_ticks(uint32_t ticks) {
    if (ticks) {
        _delay_counts = ticks / _tick_divisor;
    }
}

static void start_timer() {
    // We use the first call to start_timer() as a commit point for the
    // PIO engine initialization because we do not know the final set
    // of step pins until now.  init() is called first, then init_step_pin()
    // is called some number of times.  Eventually start_timer() is called
    // prior to actually performing steps.
    if (!_pio_inited) {
        // pulse_us can be as much as 20 us.  We run the PIO engine at 2 MHz
        // so it could take as many as 40 idle cycles to time that, but
        // the max idle count for one PIO instruction is 31.  Fortunately
        // we can distribute it over 2 instructions, so we calculate two
        // idle count values that add up to the requirement.
        int32_t delay_cycles = _pulse_delay_us * PIO_MHZ;
        // -2 accounts for the fixed overhead
        int32_t delay1 = delay_cycles - 2;
        int32_t delay2 = 0;
        if (delay1 < 0) {
            delay1 = 0;
        } else if (delay1 > 31) {
            delay2 = delay1 - 31;
            delay1 = 31;
        }
        if (delay2 > 31) {
            delay2 = 31;
        }

        const int len = 4;

        static uint16_t instructions[len];

        // We use autopull, so the first OUT instruction will stall until
        // the OSR has received data from the FIFO

        // Set up to 16 step pins, delaying for the pulse_us time
        instructions[0] = pio_encode_out(pio_pins, 16) | pio_encode_delay(delay1);
        // Get inter-pulse delay count from the rest of the FIFO word
        instructions[1] = pio_encode_out(pio_x, 16) | pio_encode_delay(delay2);
        // Restore the step pins to the unstep value which was previously loaded into Y
        instructions[2] = pio_encode_mov(pio_pins, pio_y);
        // Spin for the inter-pulse delay time
        instructions[3] = pio_encode_jmp_x_dec(len - 1);

        pio_program_t program = { .instructions = instructions, .length = len, .origin = -1 };

        _sm = pio_claim_unused_sm(_pio, false);
        if (_sm < 0) {
            // Shouldn't happen
            return;
        }

        uint32_t offset = pio_add_program(_pio, &program);

        // Relocate the JMP target according to where the program was loaded
        _pio->instr_mem[offset + len - 1] = pio_encode_jmp_x_dec(offset + len - 1);

        pio_sm_config cfg = pio_get_default_sm_config();

        int8_t num_pins = _max_step_pin - _min_step_pin + 1;
        sm_config_set_out_pins(&cfg, _min_step_pin, num_pins);

        sm_config_set_out_shift(&cfg, true /* right */, true /* autopull */, 32);

        uint32_t sys_hz = clock_get_hz(clk_sys);
        sm_config_set_clkdiv(&cfg, (float)sys_hz / (PIO_MHZ * 1000000.0f));

        sm_config_set_wrap(&cfg, offset, offset + len - 1);

        pio_sm_init(_pio, _sm, offset, &cfg);

        // Shift the unstep bitmask so the first step pin is at bit 0,
        _unstep_bits >>= _min_step_pin;

        // Push the unstep bitmask to the TX FIFO
        pio_sm_put(_pio, _sm, _unstep_bits);

        // Read the FIFO value into the OSR
        pio_sm_exec(_pio, _sm, pio_encode_pull(false, false));

        // Copy the OSR to the Y register for later use when turning off steps
        pio_sm_exec(_pio, _sm, pio_encode_mov(pio_y, pio_osr));

        pio_sm_set_enabled(_pio, _sm, true);

        _pio_irq_init();

        _pio_inited = true;
    }
    _pio_irq_arm();
}

static void stop_timer() {}

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
