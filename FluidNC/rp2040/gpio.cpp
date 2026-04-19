// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// RP2040 GPIO driver implementing fluidnc_gpio.h interface

#include "Driver/fluidnc_gpio.h"
#include "Protocol.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include <cstring>

#define MAX_GPIO_PINS 30  // RP2040 has GPIO 0-29

// Callback structures for GPIO interrupts
struct GPIOCallback {
    void (*callback)(void*);
    void* arg;
};

static GPIOCallback gpio_callbacks[MAX_GPIO_PINS] = { 0 };

// GPIO event tracking (for poll-based event system)
static uint32_t gpios_inverted = 0;    // GPIOs that are active low (inverted)
static uint32_t gpios_interest = 0;    // GPIOs with event handlers
static uint32_t gpios_current = 0;     // Current state of GPIOs
static void* gpioArgs[MAX_GPIO_PINS] = { nullptr };

// GPIO interrupt handler
static void gpio_interrupt_handler(uint gpio, uint32_t events) {
    if (gpio < MAX_GPIO_PINS && gpio_callbacks[gpio].callback) {
        gpio_callbacks[gpio].callback(gpio_callbacks[gpio].arg);
    }
}

// Write GPIO pin
void gpio_write(pinnum_t pin, bool value) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) {
        return;
    }
    gpio_put((uint)pin, value);
}

// Read GPIO pin
bool gpio_read(pinnum_t pin) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) {
        return false;
    }
    return gpio_get((uint)pin);
}

// Configure GPIO mode
void gpio_mode(pinnum_t pin, bool input, bool output, bool pullup, bool pulldown, bool opendrain) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) {
        return;
    }

    uint gpio = (uint)pin;

    gpio_init(gpio);

    // Configure pull-ups/pull-downs
    if (pullup) {
        gpio_pull_up(gpio);
    } else if (pulldown) {
        gpio_pull_down(gpio);
    } else {
        gpio_disable_pulls(gpio);
    }

    // Configure direction
    if (input && output) {
        // Bidirectional - configure as output (can also read)
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_set_input_enabled(gpio, true);
    } else if (output) {
        gpio_set_dir(gpio, GPIO_OUT);
    } else if (input) {
        gpio_set_dir(gpio, GPIO_IN);
    }

    // Note: RP2040 GPIO doesn't have native open-drain mode in hardware GPIO control
    // Open drain would need to be emulated via driver logic if needed
    // For now, we ignore the opendrain parameter
}

// Set GPIO drive strength (0-3 for RP2040)
void gpio_drive_strength(pinnum_t pin, uint8_t strength) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) {
        return;
    }
    
    // RP2040 GPIO drive strength is controlled via CTRL register
    // For simplicity, we just validate the strength value
    // Full implementation would require register-level access to set slew rate/drive strength
    if (strength > 3) {
        strength = 3;
    }
    
    // TODO: Implement actual drive strength control via hardware registers
    // For now, this is a no-op as pico-sdk doesn't expose drive strength control
}

// Set GPIO interrupt type (edge, level, etc.)
void gpio_set_interrupt_type(pinnum_t pin, uint8_t mode) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) {
        return;
    }
    
    uint gpio = (uint)pin;
    uint32_t events = 0;
    
    // RP2040 GPIO interrupt types
    switch (mode) {
        case 0:  // Disable
            events = 0;
            break;
        case 1:  // Rising edge
            events = GPIO_IRQ_EDGE_RISE;
            break;
        case 2:  // Falling edge
            events = GPIO_IRQ_EDGE_FALL;
            break;
        case 3:  // Both edges
            events = GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL;
            break;
        case 4:  // High level
            events = GPIO_IRQ_LEVEL_HIGH;
            break;
        case 5:  // Low level
            events = GPIO_IRQ_LEVEL_LOW;
            break;
        default:
            events = 0;
    }
    
    // Apply the interrupt configuration
    gpio_set_irq_enabled(gpio, events, events != 0);
}

// Add interrupt handler for a GPIO pin
void gpio_add_interrupt(pinnum_t pin, int8_t mode, void (*callback)(void*), void* arg) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) {
        return;
    }
    
    uint gpio = (uint)pin;
    
    // Store the callback
    gpio_callbacks[gpio].callback = callback;
    gpio_callbacks[gpio].arg = arg;
    
    // Set up the global interrupt handler if not already done
    static bool irq_initialized = false;
    if (!irq_initialized) {
        gpio_set_irq_enabled_with_callback(0, 0, false, gpio_interrupt_handler);
        irq_initialized = true;
    }
    
    // Configure interrupt type
    gpio_set_interrupt_type(pin, mode);
}

// Remove interrupt handler for a GPIO pin
void gpio_remove_interrupt(pinnum_t pin) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) {
        return;
    }
    
    uint gpio = (uint)pin;
    
    // Disable the interrupt
    gpio_set_irq_enabled(gpio, 0xFF, false);
    
    // Clear the callback
    gpio_callbacks[gpio].callback = nullptr;
    gpio_callbacks[gpio].arg = nullptr;
}

// Route a signal to GPIO output
void gpio_route(pinnum_t pin, uint32_t signal) {
    if (pin < 0 || pin >= MAX_GPIO_PINS) {
        return;
    }
    
    // RP2040 doesn't have the same signal routing as ESP32
    // This is primarily used for stepping/direction/enable signals
    // For now, just ensure the pin is configured as output
    uint gpio = (uint)pin;
    gpio_set_dir(gpio, GPIO_OUT);
}

// Get current GPIO state with inversion applied
static inline uint32_t get_gpios() {
    return gpio_get_all() ^ gpios_inverted;
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

