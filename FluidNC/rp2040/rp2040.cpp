// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// RP2040-specific initializations

#include <Arduino.h>
#include "Main.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "StepTimer.h"
#include "ws2812.h"

#define LED_PIN 16  // GPIO 16 for WS2812B NeoPixel

// PIO state machine for WS2812
static PIO  ws2812_pio = pio0;
static uint ws2812_sm  = 0;

void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(ws2812_pio, ws2812_sm, pixel_grb << 8u);
}

void put_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    uint32_t grb = (green << 16) | (red << 8) | blue;
    put_pixel(grb);
}

// Platform pre-initialization - called after FreeRTOS is started but before setup()
void platform_preinit() {
    // Initialize PIO for WS2812 LED
    // uint offset = pio_add_program(ws2812_pio, &ws2812_program);
    // ws2812_program_init(ws2812_pio, ws2812_sm, offset, LED_PIN, 800000, false);  // 800kHz for WS2812B

    // Red
    //    put_rgb(50, 0, 0);
    //    delay(1000);  // 1000ms

    // Green
    //    put_rgb(0, 50, 0);
    //    delay(1000);  // 1000ms

    // Blue
    //    put_rgb(0, 0, 50);
    //    delay(1000);  // 1000ms

    delay(1000);
    // Initialize Serial (USB CDC)
    Serial.begin(115200);

    delay(2000);
    Serial.println("Starting");

    if (BOOTSEL) {
        Serial.println("Release the BOOT button");
        while (BOOTSEL) {
            delay(1);
        }
    }

    // Off
    //    put_rgb(0, 0, 0);
    //    delay(1000);  // 1000ms

    // FluidNC running: LED to yellow
    //    put_rgb(255, 255, 0);
}

void rp2040_init() {
    // Initialize RP2040-specific hardware

    // Note: pico_stdlib is initialized automatically by PlatformIO
    // All basic I/O initialization happens here
}

void rp2040_startup_message() {
    // Print startup information specific to RP2040
}
