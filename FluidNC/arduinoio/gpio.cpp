// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// RP2040 GPIO driver implementing fluidnc_gpio.h interface

#include "Driver/fluidnc_gpio.h"
#include "Protocol.h"
#include <cstring>
#include <Arduino.h>

#define MAX_GPIO_PINS 30  // RP2040 has GPIO 0-29

// Write GPIO pin
void gpio_write(pinnum_t pin, bool value) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) {
        return;
    }
    digitalWrite(pin, value);
}

// Read GPIO pin
bool gpio_read(pinnum_t pin) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) {
        return false;
    }
    return digitalRead(pin);
}

// Configure GPIO mode
void gpio_mode(pinnum_t pin, bool input, bool output, bool pullup, bool pulldown, bool opendrain) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) {
        return;
    }

    if (output) {
        pinMode(pin, OUTPUT);
    } else if (pullup) {
        pinMode(pin, INPUT_PULLUP);
    } else if (pulldown) {
        pinMode(pin, INPUT_PULLDOWN);
    } else {
        pinMode(pin, INPUT);
    }
}

// Set GPIO drive strength (0-3 for RP2040)
void gpio_drive_strength(pinnum_t pin, uint8_t strength) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) {
        return;
    }
    gpio_init(pin);
    switch (strength) {
        case 0:
            pinMode(pin, OUTPUT_2MA);
            break;
        case 1:
            pinMode(pin, OUTPUT_4MA);
            break;
        case 2:
            pinMode(pin, OUTPUT_8MA);
            break;
        case 3:
            pinMode(pin, OUTPUT_12MA);
            break;
    }
}

// Route a signal to GPIO output
void gpio_route(pinnum_t pin, uint32_t signal) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) {
        return;
    }
}

// Get current GPIO state with inversion applied
static inline uint32_t get_gpios() {
    uint32_t state = 0;
    for (int i = 0; i < MAX_GPIO_PINS; i++) {
        if (digitalRead(i)) {
            state |= (1 << i);
        }
    }
    return state ^ gpios_inverted;
}

// Set up event tracking for a GPIO pin
void gpio_set_event(int32_t gpio_num, void* arg, bool invert) {
    if (gpio_num < 0 || gpio_num >= MAX_GPIO_PINS) {
        return;
    }
    
    gpioArgs[gpio_num] = arg;
    
    // Mark this GPIO as having an event handler
    gpios_interest |= (1 << gpio_num);
    
    // Store inversion flag
    if (invert) {
        gpios_inverted |= (1 << gpio_num);
    } else {
        gpios_inverted &= ~(1 << gpio_num);
    }
    
    // Initialize current state to the opposite of actual state
    // so the first poll will detect and send the actual state
    bool active = gpio_read(gpio_num) ^ invert;
    if (active) {
        gpios_current &= ~(1 << gpio_num);  // Set opposite in current
    } else {
        gpios_current |= (1 << gpio_num);   // Set opposite in current
    }
}

// Clear event tracking for a GPIO pin
void gpio_clear_event(int32_t gpio_num) {
    if (gpio_num < 0 || gpio_num >= MAX_GPIO_PINS) {
        return;
    }
    
    gpioArgs[gpio_num] = nullptr;
    gpios_interest &= ~(1 << gpio_num);
}

// Poll GPIO states and send events
void poll_gpios() {
    uint32_t gpios_active = get_gpios();
    uint32_t gpios_changed = (gpios_active ^ gpios_current) & gpios_interest;
    
    // Process each changed GPIO
    for (int i = 0; i < MAX_GPIO_PINS; i++) {
        if (gpios_changed & (1 << i)) {
            bool active = (gpios_active & (1 << i)) != 0;
            
            // Send event if we have an arg (callback)
            if (gpioArgs[i]) {
                const Event* event = active ? &pinActiveEvent : &pinInactiveEvent;
                protocol_send_event_from_ISR(event, gpioArgs[i]);
            }
            
            // Update current state
            if (active) {
                gpios_current |= (1 << i);
            } else {
                gpios_current &= ~(1 << i);
            }
        }
    }
}

