// Copyright (c) 2020 - Michiyasu Odaki
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

// It should be included at the outset to know the machine configuration.
#include "Config.h"

#include <stdint.h>

#include "Pin.h"

/* Assert */
#if defined(I2S_OUT_NUM_BITS)
#    if (I2S_OUT_NUM_BITS != 16) && (I2S_OUT_NUM_BITS != 32)
#        error "I2S_OUT_NUM_BITS should be 16 or 32"
#    endif
#else
#    define I2S_OUT_NUM_BITS 32
#endif

// #    define I2SO(n) (I2S_OUT_PIN_BASE + n)

/* 16-bit mode: 1000000 usec / ((160000000 Hz) / 10 / 2) x 16 bit/pulse x 2(stereo) = 4 usec/pulse */
/* 32-bit mode: 1000000 usec / ((160000000 Hz) /  5 / 2) x 32 bit/pulse x 2(stereo) = 4 usec/pulse */
const uint32_t I2S_OUT_USEC_PER_PULSE = 4;

constexpr uint32_t i2s_out_max_steps_per_sec = 1000000 / (2 * I2S_OUT_USEC_PER_PULSE);

const int I2S_OUT_DMABUF_COUNT = 5;    /* number of DMA buffers to store data */
const int I2S_OUT_DMABUF_LEN   = 2000; /* maximum size in bytes (4092 is DMA's limit) */

const int I2S_OUT_DELAY_DMABUF_MS = (I2S_OUT_DMABUF_LEN / sizeof(uint32_t) * I2S_OUT_USEC_PER_PULSE / 1000);
const int I2S_OUT_DELAY_MS        = (I2S_OUT_DELAY_DMABUF_MS * (I2S_OUT_DMABUF_COUNT + 1));

typedef struct {
    /*
        I2S bitstream (32-bits): Transfers from MSB(bit31) to LSB(bit0) in sequence
        ------------------time line------------------------>
             Left Channel                    Right Channel
        ws   ________________________________~~~~...
        bck  _~_~_~_~_~_~_~_~_~_~_~_~_~_~_~_~_~_~...
        data vutsrqponmlkjihgfedcba9876543210
             XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
                                             ^
                                Latches the X bits when ws is switched to High
        If I2S_OUT_PIN_BASE is set to 128,
        bit0:Expanded GPIO 128, 1: Expanded GPIO 129, ..., v: Expanded GPIO 159
    */
    pinnum_t ws_pin;
    pinnum_t bck_pin;
    pinnum_t data_pin;
    uint32_t pulse_period;  // aka step rate.
    uint32_t init_val;
} i2s_out_init_t;

/*
  Initialize I2S out by parameters.
  return -1 ... already initialized
*/
int i2s_out_init(i2s_out_init_t& init_param);

/*
  Initialize I2S out by default parameters.
    i2s_out_init_t default_param = {
        .ws_pin = I2S_OUT_WS,
        .bck_pin = I2S_OUT_BCK,
        .data_pin = I2S_OUT_DATA,
        .pulse_period = I2S_OUT_USEC_PER_PULSE,
        .init_val = I2S_OUT_INIT_VAL,
    };
  return -1 ... already initialized
*/
int i2s_out_init();

/*
  Read a bit state from the internal pin state var.
  pin: expanded pin No. (0..31)
*/
uint8_t i2s_out_read(pinnum_t pin);

void i2s_out_push();

/*
   Set a bit in the internal pin state var. (not written electrically)
   pin: expanded pin No. (0..31)
   val: bit value(0 or not 0)
*/
void i2s_out_write(pinnum_t pin, uint8_t val);

/*
    Set current pin state to the I2S bitstream buffer
    (This call will generate a future I2S_OUT_USEC_PER_PULSE μs x N bitstream)
    usec: The length of time that the pulse should be repeated.
         That time will be converted to an integer number of pulses of
         length I2S_OUT_USEC_PER_PULSE.
         The number of samples is limited to (20 / I2S_OUT_USEC_PER_PULSE).
    return: number of pushed samples
            0 .. no space for push
 */
void i2s_out_push_sample(uint32_t usec);

/*
   Set pulser mode to passtrough
   After this function is called,
   the callback function to generate the pulse data
   will not be called.
 */
int i2s_out_set_passthrough();

/*
   Set pulser mode to stepping
   After this function is called,
   the callback function to generate stepping pulse data
   will be called.
 */
int i2s_out_set_stepping();

/*
  Dynamically delay until the Shift Register Pin changes
  according to the current I2S processing state and mode.
 */
void i2s_out_delay();

/*
   Set the pulse callback period in microseconds
 */
int i2s_out_set_pulse_period(uint32_t usec);

/*
   Get current pulser mode
 */
enum i2s_out_pulser_status_t {
    PASSTHROUGH = 0,  // Static I2S mode.The i2s_out_write() reflected with very little delay
    STEPPING,         // Streaming step data.
    WAITING,          // Waiting for the step DMA completion
};
i2s_out_pulser_status_t IRAM_ATTR i2s_out_get_pulser_status();

/*
   Reset i2s I/O expander
   - Stop ISR/DMA
   - Clear DMA buffer with the current expanded GPIO bits
   - Retart ISR/DMA
 */
int i2s_out_reset();

/*
   Reference: "ESP32 Technical Reference Manual" by Espressif Systems
     https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf
 */
