// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "UserOutputs.h"
#include "../Config.h"      // log_*
#include <esp32-hal-cpu.h>  // getApbFrequency()

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
                log_info("User Digital Output:" << i << " on Pin:" << pin.name());
            }
        }
        // determine the highest resolution (number of precision bits) allowed by frequency
        uint32_t apb_frequency = getApbFrequency();

        for (int i = 0; i < MaxUserAnalogPin; ++i) {
            uint8_t resolution_bits;
            Pin&    pin = _analogOutput[i];
            if (pin.defined()) {
                _pwm[i] = new PwmPin(pin, _analogFrequency[i]);
                _pwm[i]->setDuty(0);
                log_info("User Analog Output " << i << " on Pin:" << pin.name() << " Freq:" << _pwm[i]->frequency() << "Hz");
            }
        }
    }

    void UserOutputs::all_off() {
        for (size_t io_num = 0; io_num < MaxUserDigitalPin; io_num++) {
            setDigital(io_num, false);
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

        uint32_t duty = uint32_t(percent / 100.0f * _denominator[io_num]);

        auto pwm = _pwm[io_num];
        if (!pwm) {
            log_error("M67 PWM channel error");
            return false;
        }

        if (_current_value[io_num] == duty) {
            return true;
        }

        _current_value[io_num] = duty;

        pwm->setDuty(duty);

        return true;
    }

    void UserOutputs::group(Configuration::HandlerBase& handler) {
        handler.item("analog0_pin", _analogOutput[0]);
        handler.item("analog1_pin", _analogOutput[1]);
        handler.item("analog2_pin", _analogOutput[2]);
        handler.item("analog3_pin", _analogOutput[3]);
        handler.item("analog0_hz", _analogFrequency[0], 1, 20000000);
        handler.item("analog1_hz", _analogFrequency[1], 1, 20000000);
        handler.item("analog2_hz", _analogFrequency[2], 1, 20000000);
        handler.item("analog3_hz", _analogFrequency[3], 1, 20000000);
        handler.item("digital0_pin", _digitalOutput[0]);
        handler.item("digital1_pin", _digitalOutput[1]);
        handler.item("digital2_pin", _digitalOutput[2]);
        handler.item("digital3_pin", _digitalOutput[3]);
    }
}
