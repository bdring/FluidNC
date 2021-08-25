// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"
#include "../GCode.h"  // MaxUserDigitalPin
#include "../UserOutput.h"

namespace Machine {
    class UserOutputs : public Configuration::Configurable {
        UserOutput::AnalogOutput*  myAnalogOutputs[MaxUserDigitalPin];
        UserOutput::DigitalOutput* myDigitalOutputs[MaxUserDigitalPin];

    public:
        UserOutputs();

        Pin _analogOutput[MaxUserDigitalPin];
        int _analogFrequency[MaxUserDigitalPin];
        Pin _digitalOutput[MaxUserDigitalPin];

        void init();
        void all_off();

        void group(Configuration::HandlerBase& handler) override;
        bool setDigital(size_t io_num, bool isOn);
        bool setAnalogPercent(size_t io_num, float percent);

        ~UserOutputs() = default;
    };
}
