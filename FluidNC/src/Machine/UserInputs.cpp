// Copyright (c) 2024 - Dylan Knutson
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "UserInputs.h"

namespace Machine {
    UserInputs::UserInputs() {}
    UserInputs::~UserInputs() {}

    // clang-format off
    InputPin UserInputs::digitalInput[MaxUserDigitalPin] = {
        InputPin { "digital0_pin" },
        InputPin { "digital1_pin" },
        InputPin { "digital2_pin" },
        InputPin { "digital3_pin" },
        InputPin { "digital4_pin" },
        InputPin { "digital5_pin" },
        InputPin { "digital6_pin" },
        InputPin { "digital7_pin" },
    };
    InputPin UserInputs::analogInput[MaxUserAnalogPin] = {
        InputPin { "analog0_pin" },
        InputPin { "analog1_pin" },
        InputPin { "analog2_pin" },
        InputPin { "analog3_pin" },
    };
    // clang-format on

    void UserInputs::group(Configuration::HandlerBase& handler) {
        for (size_t i = 0; i < MaxUserDigitalPin; i++) {
            auto& pin = digitalInput[i];
            handler.item(pin.legend(), pin);
        }
        for (size_t i = 0; i < MaxUserAnalogPin; i++) {
            auto& pin = analogInput[i];
            handler.item(pin.legend(), pin);
        }
    }

    void UserInputs::init() {
        for (size_t i = 0; i < MaxUserDigitalPin; i++) {
            auto& pin = digitalInput[i];
            if (pin.defined()) {
                pin.init();
            }
        }
        for (size_t i = 0; i < MaxUserAnalogPin; i++) {
            auto& pin = analogInput[i];
            if (pin.defined()) {
                pin.init();
            }
        }
    }

}  // namespace Machine
