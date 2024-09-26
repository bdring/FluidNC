// Copyright (c) 2024 - Dylan Knutson
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "UserInputs.h"

namespace Machine {
    UserInputs::UserInputs() {}
    UserInputs::~UserInputs() {}

    void UserInputs::group(Configuration::HandlerBase& handler) {
        char item_name[64];
        for (size_t i = 0; i < MaxUserAnalogPin; i++) {
            snprintf(item_name, sizeof(item_name), "analog%d_pin", i);
            handler.item(item_name, _analogInput[i].pin);
            _analogInput[i].name = item_name;
        }
        for (size_t i = 0; i < MaxUserDigitalPin; i++) {
            snprintf(item_name, sizeof(item_name), "digital%d_pin", i);
            handler.item(item_name, _digitalInput[i].pin);
            _digitalInput[i].name = item_name;
        }
    }

    void UserInputs::init() {
        for (auto& input : _analogInput) {
            if (input.pin.defined()) {
                input.pin.setAttr(Pin::Attr::Input);
                log_info("User Analog Input: " << input.name << " on Pin " << input.pin.name());
            }
        }
        for (auto& input : _digitalInput) {
            if (input.pin.defined()) {
                input.pin.setAttr(Pin::Attr::Input);
                log_info("User Digital Input: " << input.name << " on Pin " << input.pin.name());
            }
        }
    }

    UserInputs::ReadInputResult UserInputs::readDigitalInput(uint8_t input_number) {
        if (input_number >= MaxUserDigitalPin) {
            return Error::PParamMaxExceeded;
        }
        auto& input = _digitalInput[input_number];
        if (!input.pin.defined()) {
            return Error::InvalidValue;
        }
        return input.pin.read();
    }

    UserInputs::ReadInputResult UserInputs::readAnalogInput(uint8_t input_number) {
        // TODO - analog pins are read the same as digital.
        if (input_number >= MaxUserAnalogPin) {
            return Error::PParamMaxExceeded;
        }
        auto& input = _analogInput[input_number];
        if (!input.pin.defined()) {
            return Error::InvalidValue;
        }
        return input.pin.read();
    }

}  // namespace Machine
