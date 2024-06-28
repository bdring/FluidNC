// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

// PWM driver interface

#include "src/Pin.h"

class PwmPin {
public:
    PwmPin(const Pin& pin, uint32_t frequency);
    ~PwmPin();
    uint32_t frequency() { return _frequency; }
    uint32_t period() { return _period; }

    void setDuty(uint32_t duty);

private:
    uint32_t _frequency;
    int      _channel;
    int      _period;
    int      _gpio;
};
