// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#include "Uart.h"
#include "Protocol.h"
#include "Driver/fluidnc_gpio.h"
#include "Pin.h"

#include "driver/gpio.h"
#include "hal/gpio_hal.h"
#include "rom/gpio.h"  // gpio_matrix_*

static gpio_dev_t* _gpio_dev = GPIO_HAL_GET_HW(GPIO_PORT_0);

void IRAM_ATTR gpio_write(pinnum_t pin, bool value) {
    gpio_ll_set_level(_gpio_dev, (gpio_num_t)pin, (uint32_t)value);
}
bool IRAM_ATTR gpio_read(pinnum_t pin) {
    return gpio_ll_get_level(_gpio_dev, (gpio_num_t)pin);
}

void gpio_mode(pinnum_t pin, bool input, bool output, bool pullup, bool pulldown, bool opendrain) {
    gpio_config_t conf {};
    conf.pin_bit_mask = (1ULL << pin);
    conf.intr_type    = GPIO_INTR_DISABLE;

    if (input) {
        conf.mode = (gpio_mode_t)((int)conf.mode | GPIO_MODE_DEF_INPUT);
    }
    if (output) {
        conf.mode = (gpio_mode_t)((int)conf.mode | GPIO_MODE_DEF_OUTPUT);
    }
    if (pullup) {
        conf.pull_up_en = GPIO_PULLUP_ENABLE;
    }
    if (pulldown) {
        conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    }
    if (opendrain) {
        conf.mode = (gpio_mode_t)((int)conf.mode | GPIO_MODE_DEF_OD);
    }
    gpio_config(&conf);
}
void gpio_drive_strength(pinnum_t pin, uint8_t strength) {
    gpio_set_drive_capability((gpio_num_t)pin, (gpio_drive_cap_t)strength);
}
#if 0
void gpio_add_interrupt(pinnum_t pin, uint8_t mode, void (*callback)(void*), void* arg) {
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);  // Will return an err if already called

    gpio_num_t gpio = (gpio_num_t)pin;
    gpio_isr_handler_add(gpio, callback, arg);

    //FIX interrupts on peripherals outputs (eg. LEDC,...)
    //Enable input in GPIO register
    gpio_hal_context_t gpiohal;
    gpiohal.dev = GPIO_LL_GET_HW(GPIO_PORT_0);
    gpio_hal_input_enable(&gpiohal, gpio);
}
void gpio_remove_interrupt(pinnum_t pin) {
    gpio_num_t gpio = (gpio_num_t)pin;
    gpio_isr_handler_remove(gpio);  //remove handle and disable isr for pin
    gpio_set_intr_type(gpio, GPIO_INTR_DISABLE);
}
#endif
void gpio_route(pinnum_t pin, uint32_t signal) {
    if (pin == INVALID_PINNUM) {
        return;
    }
    gpio_num_t gpio = (gpio_num_t)pin;
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[gpio], PIN_FUNC_GPIO);
    gpio_set_direction(gpio, (gpio_mode_t)GPIO_MODE_DEF_OUTPUT);
    gpio_matrix_out(gpio, signal, 0, 0);
}

typedef uint64_t gpio_mask_t;

static gpio_mask_t gpios_inverted = 0;  // GPIOs that are active low
static gpio_mask_t gpios_interest = 0;  // GPIOs with an action
static gpio_mask_t gpios_current  = 0;  // The last GPIO action events that were sent

static int32_t gpio_next_event_ticks[MAX_N_GPIO + 1] = { 0 };
static int32_t gpio_deltat_ticks[MAX_N_GPIO + 1]     = { 0 };

// Do not send events for changes that occur too soon
static void gpio_set_rate_limit(int32_t gpio_num, uint32_t ms) {
    gpio_deltat_ticks[gpio_num] = ms * portTICK_PERIOD_MS;
}

static inline gpio_mask_t get_gpios() {
    return ((((uint64_t)REG_READ(GPIO_IN1_REG)) << 32) | REG_READ(GPIO_IN_REG)) ^ gpios_inverted;
}
static gpio_mask_t gpio_mask(int32_t gpio_num) {
    return 1ULL << gpio_num;
}
static inline bool gpio_is_active(int32_t gpio_num) {
    return get_gpios() & gpio_mask(gpio_num);
}
static void gpios_update(gpio_mask_t& gpios, int32_t gpio_num, bool active) {
    if (active) {
        gpios |= gpio_mask(gpio_num);
    } else {
        gpios &= ~gpio_mask(gpio_num);
    }
}

static void* gpioArgs[MAX_N_GPIO + 1];

void gpio_set_event(int32_t gpio_num, void* arg, bool invert) {
    gpioArgs[gpio_num] = arg;

    gpios_update(gpios_interest, gpio_num, true);
    gpios_update(gpios_inverted, gpio_num, invert);
    gpio_set_rate_limit(gpio_num, 5);
    auto active = gpio_is_active(gpio_num);

    // Set current to the opposite of the current state so the first poll will send the current state
    gpios_update(gpios_current, gpio_num, !active);
}
void gpio_clear_event(int32_t gpio_num) {
    gpioArgs[gpio_num] = nullptr;
    gpios_update(gpios_interest, gpio_num, false);
}

static void gpio_send_event(int32_t gpio_num, bool active) {
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
}

void poll_gpios() {
    gpio_mask_t gpios_active  = get_gpios();
    gpio_mask_t gpios_changed = (gpios_active ^ gpios_current) & gpios_interest;
    if (gpios_changed) {
        int8_t zeros;
        while ((zeros = __builtin_clzll(gpios_changed)) != 64) {
            int32_t gpio_num = 63 - zeros;
            gpio_send_event(gpio_num, gpios_active & gpio_mask(gpio_num));
            // Remove bit from mask so clzll will find the next one
            gpios_update(gpios_changed, gpio_num, false);
        }
    }
}
