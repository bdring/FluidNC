// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "src/Pins/PinDetail.h"  // pinnum_t

// GPIO interface

void gpio_write(pinnum_t pin, bool value);
bool gpio_read(pinnum_t pin);
void gpio_mode(pinnum_t pin, bool input, bool output, bool pullup, bool pulldown, bool opendrain = false);
void gpio_set_interrupt_type(pinnum_t pin, int mode);
void gpio_add_interrupt(pinnum_t pin, int mode, void (*callback)(void*), void* arg);
void gpio_remove_interrupt(pinnum_t pin);
void gpio_route(pinnum_t pin, uint32_t signal);

class Print;
void gpio_dump(Print& out);

typedef void (*gpio_dispatch_t)(int, void*, bool);

void gpio_set_action(int gpio_num, gpio_dispatch_t action, void* arg, bool invert);
void gpio_clear_action(int gpio_num);
void poll_gpios();
