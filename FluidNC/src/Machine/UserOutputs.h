// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"
#include "../GCode.h"  // MaxUserDigitalPin MaxUserAnalogPin

namespace Machine {
    class UserOutputs : public Configuration::Configurable {
        uint8_t  _pwm_channel[MaxUserAnalogPin];
        uint32_t _current_value[MaxUserAnalogPin];
        uint32_t _denominator[MaxUserAnalogPin];

    public:
        UserOutputs();

        Pin _analogOutput[MaxUserAnalogPin];
        int _analogFrequency[MaxUserAnalogPin];
        Pin _digitalOutput[MaxUserDigitalPin];

        void init();
        void all_off();

        void group(Configuration::HandlerBase& handler) override;
        bool setDigital(size_t io_num, bool isOn);
        bool setAnalogPercent(size_t io_num, float percent);

        ~UserOutputs();
    };
}
