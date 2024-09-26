// Copyright (c) 2024 - Dylan Knutson
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"
#include "../GCode.h"

#include <variant>
#include <array>

namespace Machine {

    class UserInputs : public Configuration::Configurable {
        struct PinAndName {
            std::string name;
            Pin         pin;
        };

        std::array<PinAndName, MaxUserDigitalPin> _digitalInput;

        // TODO - analog pins are read the same as digital. The Pin
        // API should either be extended to support analog reads, or
        // a new AnalogPin class should be created.
        std::array<PinAndName, MaxUserAnalogPin> _analogInput;

    public:
        UserInputs();
        virtual ~UserInputs();

        void init();
        void group(Configuration::HandlerBase& handler) override;

        using ReadInputResult = std::variant<bool, Error>;
        ReadInputResult readDigitalInput(uint8_t input_number);
        ReadInputResult readAnalogInput(uint8_t input_number);
    };

}  // namespace Machine
