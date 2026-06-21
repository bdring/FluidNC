// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  PWM capabilities provided by the ESP32 LEDC controller via the ESP-IDF driver
*/

#include "Driver/PwmPin.h"
#include "Config.h"

PwmPin::PwmPin(pinnum_t gpio, bool invert, uint32_t frequency) : _gpio(gpio), _frequency(frequency) {
    _period = 1000000 / frequency;
}

// cppcheck-suppress unusedFunction
void PwmPin::setDuty(uint32_t duty) {}

PwmPin::~PwmPin() {}
