// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "UserOutputs.h"
#include "Config.h"  // log_*

namespace Machine {
    UserOutputs::UserOutputs() {
        for (int i = 0; i < MaxUserAnalogPin; ++i) {
            _analogFrequency[i] = 5000;
        }
    }
    UserOutputs::~UserOutputs() {}

    void UserOutputs::init() {
        for (int i = 0; i < MaxUserDigitalPin; ++i) {
            Pin& pin = _digitalOutput[i];
            if (pin.defined()) {
                pin.setAttr(Pin::Attr::Output);
                pin.off();
                log_info("User Digital Output: " << i << " on Pin:" << pin.name());
            }
        }

        for (int i = 0; i < MaxUserAnalogPin; ++i) {
            Pin&    pin = _analogOutput[i];
            if (pin.defined()) {
                pin.setAttr(Pin::Attr::PWM, _analogFrequency[i]);
                pin.setDuty(0);
                log_info("User Analog Output: " << i << " on Pin:" << pin.name() << " Freq:" << _analogFrequency[i] << "Hz");
            }
        }
    }

    void UserOutputs::all_off() {
        for (size_t io_num = 0; io_num < MaxUserDigitalPin; io_num++) {
            setDigital(io_num, false);
        }
        for (size_t io_num = 0; io_num < MaxUserAnalogPin; io_num++) {
            setAnalogPercent(io_num, 0);
        }
    }

    bool UserOutputs::setDigital(size_t io_num, bool isOn) {
        Pin& pin = _digitalOutput[io_num];
        if (pin.undefined()) {
            return !isOn;  // It is okay to turn off an undefined pin, for safety
        }
        pin.synchronousWrite(isOn);
        return true;
    }

    bool UserOutputs::setAnalogPercent(size_t io_num, float percent) {
        Pin& pin = _analogOutput[io_num];

        // look for errors, but ignore if turning off to prevent mass turn off from generating errors
        if (pin.undefined()) {
            return percent == 0.0;
        }

        // The 0.5 rounds to the nearest duty unit
        uint32_t duty = uint32_t(((percent * pin.maxDuty()) / 100.0f) + 0.5);
        if (_current_value[io_num] == duty) {
            return true;
        }

        _current_value[io_num] = duty;

        pin.setDuty(duty);

        return true;
    }

    void UserOutputs::group(Configuration::HandlerBase& handler) {
        // General-purpose spare output pins for M62-M65/$ commands and macros, independent
        // of any axis/spindle/coolant feature. Exactly 4 analog + 8 digital slots -- a fixed
        // array size (MaxUserAnalogPin/MaxUserDigitalPin), not extensible via config.

        // @config analog0_pin
        // @default NO_PIN
        // @pin_attributes pwm
        // Analog (PWM) output slot 0.
        handler.item("analog0_pin", _analogOutput[0]);

        // @config analog1_pin
        // @default NO_PIN
        // @pin_attributes pwm
        // Analog (PWM) output slot 1.
        handler.item("analog1_pin", _analogOutput[1]);

        // @config analog2_pin
        // @default NO_PIN
        // @pin_attributes pwm
        // Analog (PWM) output slot 2.
        handler.item("analog2_pin", _analogOutput[2]);

        // @config analog3_pin
        // @default NO_PIN
        // @pin_attributes pwm
        // Analog (PWM) output slot 3.
        handler.item("analog3_pin", _analogOutput[3]);

        // @config analog0_hz
        // @default 5000
        // PWM frequency for analog output slot 0.
        handler.item("analog0_hz", _analogFrequency[0], 1, 20000000);

        // @config analog1_hz
        // @default 5000
        // PWM frequency for analog output slot 1.
        handler.item("analog1_hz", _analogFrequency[1], 1, 20000000);

        // @config analog2_hz
        // @default 5000
        // PWM frequency for analog output slot 2.
        handler.item("analog2_hz", _analogFrequency[2], 1, 20000000);

        // @config analog3_hz
        // @default 5000
        // PWM frequency for analog output slot 3.
        handler.item("analog3_hz", _analogFrequency[3], 1, 20000000);

        // @config digital0_pin
        // @default NO_PIN
        // @pin_attributes output
        // Digital output slot 0.
        handler.item("digital0_pin", _digitalOutput[0]);

        // @config digital1_pin
        // @default NO_PIN
        // @pin_attributes output
        // Digital output slot 1.
        handler.item("digital1_pin", _digitalOutput[1]);

        // @config digital2_pin
        // @default NO_PIN
        // @pin_attributes output
        // Digital output slot 2.
        handler.item("digital2_pin", _digitalOutput[2]);

        // @config digital3_pin
        // @default NO_PIN
        // @pin_attributes output
        // Digital output slot 3.
        handler.item("digital3_pin", _digitalOutput[3]);

        // @config digital4_pin
        // @default NO_PIN
        // @pin_attributes output
        // Digital output slot 4.
        handler.item("digital4_pin", _digitalOutput[4]);

        // @config digital5_pin
        // @default NO_PIN
        // @pin_attributes output
        // Digital output slot 5.
        handler.item("digital5_pin", _digitalOutput[5]);

        // @config digital6_pin
        // @default NO_PIN
        // @pin_attributes output
        // Digital output slot 6.
        handler.item("digital6_pin", _digitalOutput[6]);

        // @config digital7_pin
        // @default NO_PIN
        // @pin_attributes output
        // Digital output slot 7.
        handler.item("digital7_pin", _digitalOutput[7]);
    }
}
