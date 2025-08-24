// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Pin.h"
#include "Uart.h"
#include "Protocol.h"
#include "Driver/fluidnc_gpio.h"

#define GPIO_NUM_MAX 40
void gpio_write(pinnum_t pin, int value) {}
int  gpio_read(pinnum_t pin) {
     return 0;
}
void gpio_mode(pinnum_t pin, int input, int output, int pullup, int pulldown, int opendrain) {}
void gpio_drive_strength(pinnum_t pin, int strength) {}
void gpio_route(pinnum_t pin, uint32_t signal) {}

typedef uint64_t gpio_mask_t;

// Can be used to display gpio_mask_t data for debugging
static const char* g_to_hex(gpio_mask_t n) {
    static char hexstr[24];
    snprintf(hexstr, 22, "0x%llx", n);
    return hexstr;
}

static gpio_mask_t gpios_inverted = 0;  // GPIOs that are active low
static gpio_mask_t gpios_interest = 0;  // GPIOs with an action
static gpio_mask_t gpios_current  = 0;  // The last GPIO action events that were sent

static int32_t gpio_next_event_ticks[GPIO_NUM_MAX + 1] = { 0 };
static int32_t gpio_deltat_ticks[GPIO_NUM_MAX + 1]     = { 0 };

// Do not send events for changes that occur too soon
static void gpio_set_rate_limit(int gpio_num, uint32_t ms) {}

static inline gpio_mask_t get_gpios() {
    return ((uint64_t(0) << 32) | 0) ^ gpios_inverted;
}
static gpio_mask_t gpio_mask(int gpio_num) {
    return 1ULL << gpio_num;
}
static inline int gpio_is_active(int gpio_num) {
    return get_gpios() & gpio_mask(gpio_num);
}
static void gpios_update(gpio_mask_t& gpios, int gpio_num, bool active) {
    if (active) {
        gpios |= gpio_mask(gpio_num);
    } else {
        gpios &= ~gpio_mask(gpio_num);
    }
}

static void* gpioArgs[GPIO_NUM_MAX + 1];

void gpio_set_event(int gpio_num, void* arg, int invert) {
    gpioArgs[gpio_num] = arg;
    gpio_mask_t mask   = gpio_mask(gpio_num);
    gpios_update(gpios_interest, gpio_num, true);
    gpios_update(gpios_inverted, gpio_num, invert);
    gpio_set_rate_limit(gpio_num, 5);
    int active = gpio_is_active(gpio_num);

    // Set current to the opposite of the current state so the first poll will send the current state
    gpios_update(gpios_current, gpio_num, !active);
}
void gpio_clear_event(int gpio_num) {
    gpioArgs[gpio_num] = nullptr;
    gpios_update(gpios_interest, gpio_num, false);
}

static void gpio_send_event(int gpio_num, bool active) {
#if 0
    auto    end_ticks  = gpio_next_event_ticks[gpio_num];
    int32_t this_ticks = int32_t(xTaskGetTickCount());
    if (end_ticks == 0 || ((this_ticks - end_ticks) > 0)) {
        end_ticks = this_ticks + gpio_deltat_ticks[gpio_num];
        if (end_ticks == 0) {
            end_ticks = 1;
        }
        gpio_next_event_ticks[gpio_num] = end_ticks;

        auto arg = gpioArgs[gpio_num];
        if (arg) {
            protocol_send_event_from_ISR(active ? &pinActiveEvent : &pinInactiveEvent, arg);
        }
        gpios_update(gpios_current, gpio_num, active);
    }
#endif
}

void poll_gpios() {
    gpio_mask_t gpios_active  = get_gpios();
    gpio_mask_t gpios_changed = (gpios_active ^ gpios_current) & gpios_interest;
    if (gpios_changed) {
        int zeros;
        while ((zeros = __builtin_clzll(gpios_changed)) != 64) {
            int gpio_num = 63 - zeros;
            gpio_send_event(gpio_num, gpios_active & gpio_mask(gpio_num));
            // Remove bit from mask so clzll will find the next one
            gpios_update(gpios_changed, gpio_num, false);
        }
    }
}
