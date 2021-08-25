// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	DacSpindle.h

	This uses the Analog DAC in the ESP32 to generate a voltage
	proportional to the GCode S value desired. Some spindle uses
	a 0-5V or 0-10V value to control the spindle. You would use
	an Op Amp type circuit to get from the 0.3.3V of the ESP32 to that voltage.
*/

#include "OnOffSpindle.h"

#include <cstdint>

namespace Spindles {
    // This uses one of the (2) DAC pins on ESP32 to output a voltage
    class Dac : public OnOff {
    public:
        Dac() = default;

        Dac(const Dac&) = delete;
        Dac(Dac&&)      = delete;
        Dac& operator=(const Dac&) = delete;
        Dac& operator=(Dac&&) = delete;

        void init() override;
        void config_message() override;
        void setSpeedfromISR(uint32_t dev_speed) override;

        // Configuration handlers:
        // Inherited from PWM

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "DAC"; }

        ~Dac() {}

    private:
        bool _gpio_ok;  // DAC is on a valid pin

    protected:
        void set_output(uint32_t duty);  // sets DAC instead of PWM
    };
}
