// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#include "Config.h"
#include "Driver/fluidnc_gpio.h"
// PWM driver interface
#include <stdint.h>

class PwmPin {
public:
    PwmPin(pinnum_t gpio, bool invert, uint32_t frequency);
    ~PwmPin();
    uint32_t frequency() { return _frequency; }
    uint32_t period() { return _period; }

    void setDuty(uint32_t duty);

private:
    pinnum_t _gpio;
    uint32_t _frequency;
    objnum_t _channel;
    uint32_t _period;
};
