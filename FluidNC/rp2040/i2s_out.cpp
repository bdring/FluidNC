// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Driver/i2s_out.h"

#include <Arduino.h>
#include <I2S.h>

namespace {
    I2S*        i2s_iface           = nullptr;
    bool        i2s_initialized     = false;
    uint32_t    i2s_out_port_data   = 0;
    uint32_t    i2s_frame_rate_hz   = 500000;
    uint32_t    i2s_min_pulse_us    = 2;

    inline uint32_t frame_rate_from_min_pulse_us(uint32_t min_pulse_us) {
        if (min_pulse_us == 0) {
            return 500000;
        }
        uint32_t rate = 1000000 / min_pulse_us;
        return rate ? rate : 500000;
    }

    inline void flush_i2s_port_data() {
        if (!i2s_initialized || i2s_iface == nullptr) {
            return;
        }
        i2s_iface->write(static_cast<int32_t>(i2s_out_port_data), true);
    }
}

extern "C" {

void i2s_out_init(i2s_out_init_t* init_param) {
    if (!init_param || i2s_initialized) {
        return;
    }

    // Arduino-Pico I2S derives LRCLK from BCLK pin assignment.
    // Keep the provided BCK/DATA and ignore explicit WS if it does not match.
    i2s_iface = new I2S(OUTPUT, init_param->bck_pin, init_param->data_pin);
    if (i2s_iface == nullptr) {
        return;
    }

    i2s_min_pulse_us  = init_param->min_pulse_us ? init_param->min_pulse_us : 2;
    i2s_frame_rate_hz = frame_rate_from_min_pulse_us(i2s_min_pulse_us);

    i2s_iface->setBitsPerSample(32);
    i2s_iface->setBuffers(4, 64, 0);

    if (!i2s_iface->begin(static_cast<long>(i2s_frame_rate_hz))) {
        delete i2s_iface;
        i2s_iface = nullptr;
        return;
    }

    i2s_out_port_data = init_param->init_val;
    i2s_initialized   = true;
    flush_i2s_port_data();
}

uint8_t i2s_out_read(pinnum_t pin) {
    if (pin < 0 || pin >= I2S_OUT_NUM_BITS) {
        return 0;
    }
    return (i2s_out_port_data >> pin) & 1U;
}

void i2s_out_write(pinnum_t pin, uint8_t val) {
    if (pin < 0 || pin >= I2S_OUT_NUM_BITS) {
        return;
    }
    uint32_t mask = 1u << pin;
    if (val) {
        i2s_out_port_data |= mask;
    } else {
        i2s_out_port_data &= ~mask;
    }
}

void i2s_out_delay() {
    flush_i2s_port_data();
}

}
