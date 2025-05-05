// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"
typedef uint8_t pinnum_t;

// GPIO interface

void gpio_write(pinnum_t pin, int value);
int  gpio_read(pinnum_t pin);
void gpio_mode(pinnum_t pin, int input, int output, int pullup, int pulldown, int opendrain);
void gpio_drive_strength(pinnum_t pin, int strength);
void gpio_set_interrupt_type(pinnum_t pin, int mode);
void gpio_add_interrupt(pinnum_t pin, int mode, void (*callback)(void*), void* arg);
void gpio_remove_interrupt(pinnum_t pin);
void gpio_route(pinnum_t pin, uint32_t signal);

void gpio_set_event(int gpio_num, void* arg, int invert);
void gpio_clear_event(int gpio_num);
void poll_gpios();

#ifdef __cplusplus
}
#endif
