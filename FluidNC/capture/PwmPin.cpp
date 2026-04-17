// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  PWM capabilities provided by the ESP32 LEDC controller via the ESP-IDF driver
*/

#include "Driver/PwmPin.h"
#include "Config.h"

int      g_pwmConstructCalls = 0;
int      g_pwmSetDutyCalls = 0;
pinnum_t g_pwmLastPin = INVALID_PINNUM;
uint32_t g_pwmLastFrequency = 0;
uint32_t g_pwmLastDuty = 0;

PwmPin::PwmPin(pinnum_t gpio, bool invert, uint32_t frequency) : _gpio(gpio), _frequency(frequency) {
    ++g_pwmConstructCalls;
    g_pwmLastPin = gpio;
    g_pwmLastFrequency = frequency;
    _period = 1000000 / frequency;
}

// cppcheck-suppress unusedFunction
void PwmPin::setDuty(uint32_t duty) {
    ++g_pwmSetDutyCalls;
    g_pwmLastDuty = duty;
}

PwmPin::~PwmPin() {}
