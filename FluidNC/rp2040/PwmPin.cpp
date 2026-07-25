// Copyright 2025 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  PWM capabilities for RP2040 using hardware PWM slices
*/

#include "Config.h"
#include "Driver/PwmPin.h"
#include <hardware/pwm.h>
#include <hardware/gpio.h>

// RP2040 PWM provides 8 slices (0-7) with 2 channels each (A and B)
// Total of 16 PWM outputs available on GPIO 0-29 (28 usable, 29 reserved)
// Each slice can run at a different frequency

static uint8_t allocateSlice() {
    static uint8_t nextSlice = 0;
    uint8_t slice = nextSlice;
    if (nextSlice < 8) {
        nextSlice++;
    }
    return slice;
}

// Calculate PWM divider and top value to achieve desired frequency
// RP2040 clock is 125 MHz
static void calculate_pwm_config(uint32_t frequency, uint16_t& divider, uint16_t& wrap) {
    const uint32_t clock_hz = 125000000;  // 125 MHz system clock
    
    // PWM period = (1 + wrap) / (clock_hz / (128 * divider))
    // We want period = clock_hz / frequency
    // So wrap + 1 = (clock_hz / frequency) / (128 * divider)
    
    // Start with divider of 1.0 (16 in hardware, since hardware divider is 0.0 = 1.0)
    divider = 1;
    wrap = clock_hz / frequency - 1;
    
    // If wrap > 65535, we need a higher divider
    uint8_t div_int = 1;
    while (wrap > 65535 && div_int < 255) {
        div_int++;
        wrap = clock_hz / (frequency * div_int) - 1;
    }
    
    // Hardware divider is in fixed-point (Q16)
    divider = div_int << 4;  // Convert to fixed-point Q4.4 format
    if (wrap == 0) wrap = 1;  // Minimum wrap value
}

PwmPin::PwmPin(pinnum_t gpio, bool isActiveLow, uint32_t frequency) 
    : _gpio(gpio), _frequency(frequency) {
    
    // Allocate a PWM slice for this pin
    uint8_t slice = allocateSlice();
    _channel = slice;
    
    // Determine which channel (A or B) on the slice
    bool is_channel_b = (gpio % 2) == 1;
    
    // Calculate PWM configuration
    uint16_t divider, wrap;
    calculate_pwm_config(frequency, divider, wrap);
    _period = wrap;
    
    // Configure the PWM slice
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&cfg, divider >> 4);  // Extract integer part
    pwm_config_set_clkdiv_int_frac(&cfg, divider >> 4, (divider & 0xF) << 4);
    pwm_config_set_wrap(&cfg, wrap);
    if (isActiveLow) {
        pwm_config_set_output_polarity(&cfg, false, true);  // Invert channel B (or A as needed)
    }
    
    // Set GPIO to PWM function
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    
    // Initialize PWM on the slice
    pwm_init(slice, &cfg, false);
    
    // Set initial duty to 0
    setDuty(0);
    
    // Start PWM
    pwm_set_enabled(slice, true);
}

void IRAM_ATTR PwmPin::setDuty(uint32_t duty) {
    uint8_t slice = _channel & 0x0F;
    bool channel_b = (_gpio % 2) == 1;
    
    // Clamp duty to period
    if (duty > _period) {
        duty = _period;
    }
    
    if (channel_b) {
        pwm_set_chan_level(slice, PWM_CHAN_B, duty);
    } else {
        pwm_set_chan_level(slice, PWM_CHAN_A, duty);
    }
}

PwmPin::~PwmPin() {
    uint8_t slice = _channel & 0x0F;
    pwm_set_enabled(slice, false);
}
