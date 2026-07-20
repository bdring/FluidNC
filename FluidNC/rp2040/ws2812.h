// --------- //
// ws2812.h //
// --------- //

#ifndef _WS2812_H_
#define _WS2812_H_

#include "hardware/pio.h"
#include "hardware/clocks.h"

#ifdef __cplusplus
extern "C" {
#endif

// Embedded PIO program for WS2812
static const uint16_t ws2812_program_instructions[] = {
    //     .wrap_target
    0x6221, //  0: out      x, 1            side 0 [2]
    0x1123, //  1: jmp      4               side 1 [1]
    0x1400, //  2: jmp      0               side 1 [4]
    0x1400, //  3: jmp      0               side 0 [4]
    //     .wrap
};

static const pio_program_t ws2812_program = {
    .instructions = ws2812_program_instructions,
    .length = 4,
    .origin = -1,
};

static inline pio_sm_config ws2812_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + 0, offset + 3);
    sm_config_set_sideset(&c, 1, false, false);
    return c;
}

static inline void ws2812_program_init(PIO pio, uint sm, uint offset, uint pin, uint freq, bool rgbw) {
    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    
    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_out_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    
    int cycles_per_bit = clock_get_hz(clk_sys) / freq;
    sm_config_set_clkdiv(&c, cycles_per_bit);
    
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

#ifdef __cplusplus
}
#endif

#endif

