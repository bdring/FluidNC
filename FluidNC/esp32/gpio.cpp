// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "src/Pin.h"
#include "src/Uart.h"
#include "Driver/fluidnc_gpio.h"

#include "driver/gpio.h"
#include "hal/gpio_hal.h"

#include "src/Logging.h"
#include "src/Protocol.h"

#include <vector>

void gpio_write(pinnum_t pin, bool value) {
    gpio_set_level((gpio_num_t)pin, value);
}
bool gpio_read(pinnum_t pin) {
    return gpio_get_level((gpio_num_t)pin);
}
void gpio_mode(pinnum_t pin, bool input, bool output, bool pullup, bool pulldown, bool opendrain) {
    gpio_config_t conf = { .pin_bit_mask = (1ULL << pin), .intr_type = GPIO_INTR_DISABLE };

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
    log_debug("gpio conf " << conf.pull_up_en << " " << pin);
    gpio_config(&conf);
}
void IRAM_ATTR gpio_set_interrupt_type(pinnum_t pin, int mode) {
    gpio_int_type_t type = GPIO_INTR_DISABLE;
    // Do not use switch here because it is not IRAM_ATTR safe
    if (mode == Pin::RISING_EDGE) {
        type = GPIO_INTR_POSEDGE;
    } else if (mode == Pin::FALLING_EDGE) {
        type = GPIO_INTR_NEGEDGE;
    } else if (mode == Pin::EITHER_EDGE) {
        type = GPIO_INTR_ANYEDGE;
    }
    gpio_set_intr_type((gpio_num_t)pin, type);
}

void gpio_add_interrupt(pinnum_t pin, int mode, void (*callback)(void*), void* arg) {
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);  // Will return an err if already called

    gpio_set_interrupt_type(pin, mode);

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
void gpio_route(pinnum_t pin, uint32_t signal) {
    if (pin == 255) {
        return;
    }
    gpio_num_t gpio = (gpio_num_t)pin;
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[gpio], PIN_FUNC_GPIO);
    gpio_set_direction(gpio, (gpio_mode_t)GPIO_MODE_DEF_OUTPUT);
    gpio_matrix_out(gpio, signal, 0, 0);
}
static bool is_input(gpio_num_t gpio) {
    return GET_PERI_REG_MASK(GPIO_PIN_MUX_REG[gpio], FUN_IE);
}
static bool is_output(gpio_num_t gpio) {
    if (gpio < 32) {
        return GET_PERI_REG_MASK(GPIO_ENABLE_REG, 1 << gpio);
    } else {
        return GET_PERI_REG_MASK(GPIO_ENABLE1_REG, 1 << (gpio - 32));
    }
}
std::vector<int> avail_gpios = { 0, 1, 3, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 29, 32, 33, 34, 35, 36, 39 };

void gpio_dump() {
    for (const int& gpio : avail_gpios) {
        gpio_num_t gpio_num = static_cast<gpio_num_t>(gpio);
        if (is_input(gpio_num) && !is_output(gpio_num)) {
            log_debug(gpio);
        }
    }
}

#if 0
void IRAM_ATTR     check_switches() {
    gpio_mask_t gpio_this = REG_READ(GPIO_IN_REG);
    if ((gpio_this ^ gpio_current_l) & gpio_interest_l) {
        gpioActions
        //            protocol_send_event_from_ISR(&gpio_changed_l, (void*)gpio_this);
        protocol_send_event_from_ISR(&motionCancelEvent, (void*)gpio_this);
        gpio_current_l = gpio_this;
    }
    gpio_this = REG_READ(GPIO_IN1_REG);
    if ((gpio_this ^ gpio_current_h) & gpio_interest_h) {
        //            protocol_send_event_from_ISR(&gpio_changed_h, (void*)gpio_this);
        protocol_send_event_from_ISR(&motionCancelEvent, (void*)gpio_this);
        gpio_current_h = gpio_this;
    }
    ++gpio_interest_l;
    ++gpio_interest_h;
}
#endif

#if 0
typedef uint32_t   gpio_mask_t;
static gpio_mask_t gpio_inverts_l  = 0;
static gpio_mask_t gpio_inverts_h  = 0;
static gpio_mask_t gpio_interest_l = 0;
static gpio_mask_t gpio_interest_h = 0;
static gpio_mask_t gpio_current_l  = 0;
static gpio_mask_t gpio_current_h  = 0;
void IRAM_ATTR     check_switches() {
    gpio_mask_t gpio_this = REG_READ(GPIO_IN_REG);
    if ((gpio_this ^ gpio_current_l) & gpio_interest_l) {
        //            protocol_send_event_from_ISR(&gpio_changed_l, (void*)gpio_this);
        protocol_send_event_from_ISR(&motionCancelEvent, (void*)gpio_this);
        gpio_current_l = gpio_this;
    }
    gpio_this = REG_READ(GPIO_IN1_REG);
    if ((gpio_this ^ gpio_current_h) & gpio_interest_h) {
        //            protocol_send_event_from_ISR(&gpio_changed_h, (void*)gpio_this);
        protocol_send_event_from_ISR(&motionCancelEvent, (void*)gpio_this);
        gpio_current_h = gpio_this;
    }
    ++gpio_interest_l;
    ++gpio_interest_h;
}
#endif
#if 1
typedef uint64_t   gpio_mask_t;
static gpio_mask_t gpio_inverts  = 0;
static gpio_mask_t gpio_interest = 0;
static gpio_mask_t gpio_current  = 0;
void IRAM_ATTR     check_switches() {
    gpio_mask_t gpio_this = (((uint64_t)REG_READ(GPIO_IN_REG)) << 32) | REG_READ(GPIO_IN1_REG);
    if (gpio_this != gpio_current) {
        gpio_mask_t gpio_changes = (gpio_this ^ gpio_current) & gpio_interest;
        int         bitno;
        while (bitno = __builtin_ffsll(gpio_changes)) {
            --bitno;
            bool isActive = (gpio_this ^ gpio_inverts) & (1ULL << bitno);
            //            protocol_send_event_from_ISR(&pin_changes, (void*)(isActive ? bitno : -bitno));
            protocol_send_event_from_ISR(&motionCancelEvent, (void*)(isActive ? bitno : -bitno));
        }
        gpio_current = gpio_this;
    }
    ++gpio_interest;
}
static gpio_dispatch_t gpioActions[GPIO_NUM_MAX + 1];
static void*           gpioArgs[GPIO_NUM_MAX + 1];
void                   gpio_set_action(int gpio_num, gpio_dispatch_t action, void* arg, bool invert) {
    gpioActions[gpio_num] = action;
    gpioArgs[gpio_num]    = arg;
    gpio_mask_t mask      = 1ULL << gpio_num;
    gpio_interest |= mask;
    if (invert) {
        gpio_inverts |= mask;
    } else {
        gpio_inverts &= ~mask;
    }
}
void gpio_clear_action(int gpio_num) {
    gpioActions[gpio_num] = nullptr;
    gpioArgs[gpio_num]    = nullptr;
    gpio_mask_t mask      = 1ULL << gpio_num;
    gpio_interest &= ~mask;
}
void poll_gpios() {
    gpio_mask_t gpio_this = (((uint64_t)REG_READ(GPIO_IN1_REG)) << 32) | REG_READ(GPIO_IN_REG);

    gpio_mask_t gpio_changes = (gpio_this ^ gpio_current) & gpio_interest;
    if (gpio_changes) {
        gpio_mask_t gpio_active = gpio_this ^ gpio_inverts;
        int         zeros;
        while ((zeros = __builtin_clzll(gpio_changes)) != 64) {
            int         gpio_num = 63 - zeros;
            gpio_mask_t mask     = 1ULL << gpio_num;
            bool        isActive = gpio_active & mask;
            // Uart0 << gpio_num << " " << isActive << "\n";
            gpio_dispatch_t action = gpioActions[gpio_num];
            if (action) {
                action(gpio_num, gpioArgs[gpio_num], isActive);
            }
            gpio_changes &= ~mask;
        }
    }
    gpio_current = gpio_this;
}

#endif
