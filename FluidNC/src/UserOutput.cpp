// Copyright (c) 2020 - Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "UserOutput.h"
#include "Logging.h"         // log_*
#include "Pins/LedcPin.h"    // ledcInit()
#include <esp32-hal-ledc.h>  // ledc*
#include <esp32-hal-cpu.h>   // getApbFrequency()

namespace UserOutput {
    DigitalOutput::DigitalOutput(size_t number, Pin& pin) : _number(number), _pin(pin) {
        if (_pin.undefined()) {
            return;
        }
        init();
    }

    void DigitalOutput::init() {
        _pin.setAttr(Pin::Attr::Output);
        _pin.off();

        config_message();
    }

    void DigitalOutput::config_message() { log_info("User Digital Output:" << _number << " on Pin:" << _pin.name()); }

    bool DigitalOutput::set_level(bool isOn) {
        if (_number == UNDEFINED_OUTPUT && isOn) {
            return false;
        }

        _pin.synchronousWrite(isOn);
        return true;
    }

    // ==================================================================

    AnalogOutput::AnalogOutput(uint8_t number, Pin& pin, uint32_t pwm_frequency) :
        _number(number), _pin(pin), _pwm_frequency(pwm_frequency) {
        if (_pin.undefined()) {
            return;
        }

        // determine the highest resolution (number of precision bits) allowed by frequency
        uint32_t apb_frequency = getApbFrequency();

        // increase the precision (bits) until it exceeds allow by frequency the max or is 16
        _resolution_bits = 0;
        while ((1u << _resolution_bits) < (apb_frequency / _pwm_frequency) && _resolution_bits <= 16) {
            ++_resolution_bits;
        }
        // _resolution_bits is now just barely too high, so drop it down one
        --_resolution_bits;

        init();
    }

    void AnalogOutput::init() {
        if (_pin.undefined()) {
            return;
        }
        _pwm_channel = ledcInit(_pin, -1, _pwm_frequency, _resolution_bits);
        ledcWrite(_pwm_channel, 0);
        config_message();
    }

    void AnalogOutput::config_message() {
        log_info("User Analog Output " << _number << " on Pin:" << _pin.name() << " Freq:" << _pwm_frequency << "Hz");
    }

    // returns true if able to set value
    bool AnalogOutput::set_level(uint32_t numerator) {
        // look for errors, but ignore if turning off to prevent mask turn off from generating errors
        if (_pin.undefined()) {
            return false;
        }

        if (_pwm_channel == -1) {
            log_error("M67 PWM channel error");
            return false;
        }

        if (_current_value == numerator) {
            return true;
        }

        _current_value = numerator;

        ledcWrite(_pwm_channel, numerator);

        return true;
    }
}
