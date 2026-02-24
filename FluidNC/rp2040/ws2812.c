// Generated Raspberry Pi PIO header
// This is a placeholder implementation

#include "ws2812.h"
#include "hardware/clocks.h"

void ws2812_program_init(PIO pio, uint sm, uint offset, uint pin, uint freq, bool rgbw) {
    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    
    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_out_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TXONLY);
    
    int cycles_per_bit = clock_get_hz(clk_sys) / freq;
    sm_config_set_clkdiv(&c, cycles_per_bit);
    
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}
