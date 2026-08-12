// Copyright (c) 2024 - Dylan Knutson
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "UserInputs.h"

namespace Machine {
    UserInputs::UserInputs() {}
    UserInputs::~UserInputs() {}

    // General-purpose spare input pins, independent of any axis/spindle/coolant feature.
    // Exactly 8 digital + 4 analog slots -- a fixed array size (MaxUserDigitalPin/
    // MaxUserAnalogPin), not extensible via config. group() below registers these via a
    // loop (handler.item(pin.legend(), pin)), not a literal handler.item() call per slot --
    // see ItemDocs.md's "data-driven item lists" section for why these are annotated here,
    // at the point of registration, instead.

    // clang-format off
    InputPin UserInputs::digitalInput[MaxUserDigitalPin] = {
        // @config digital0_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 0.
        InputPin { "digital0_pin" },
        // @config digital1_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 1.
        InputPin { "digital1_pin" },
        // @config digital2_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 2.
        InputPin { "digital2_pin" },
        // @config digital3_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 3.
        InputPin { "digital3_pin" },
        // @config digital4_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 4.
        InputPin { "digital4_pin" },
        // @config digital5_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 5.
        InputPin { "digital5_pin" },
        // @config digital6_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 6.
        InputPin { "digital6_pin" },
        // @config digital7_pin
        // @default NO_PIN
        // @pin_attributes input
        // Digital input slot 7.
        InputPin { "digital7_pin" },
    };
    InputPin UserInputs::analogInput[MaxUserAnalogPin] = {
        // @config analog0_pin
        // @default NO_PIN
        // @pin_attributes input
        // Analog input slot 0.
        InputPin { "analog0_pin" },
        // @config analog1_pin
        // @default NO_PIN
        // @pin_attributes input
        // Analog input slot 1.
        InputPin { "analog1_pin" },
        // @config analog2_pin
        // @default NO_PIN
        // @pin_attributes input
        // Analog input slot 2.
        InputPin { "analog2_pin" },
        // @config analog3_pin
        // @default NO_PIN
        // @pin_attributes input
        // Analog input slot 3.
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
