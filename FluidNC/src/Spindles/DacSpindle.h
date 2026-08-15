// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"
#if MAX_N_DACS
/*
	DacSpindle.h

	This uses the Analog DAC to generate a voltage
	proportional to the GCode S value desired. Some spindle uses
	a 0-5V or 0-10V value to control the spindle. You might need
	an external OpAmp circuit to upconvert from the MCU voltage.
*/

#    include "OnOffSpindle.h"

#    include <cstdint>

namespace Spindles {
    // This uses one of the DAC pins to output a voltage
    class Dac : public OnOff {
    public:
        Dac(const char* name) : OnOff(name) {}

        Dac(const Dac&)            = delete;
        Dac(Dac&&)                 = delete;
        Dac& operator=(const Dac&) = delete;
        Dac& operator=(Dac&&)      = delete;

        void init() override;
        void config_message() override;
        void setSpeedfromISR(uint32_t dev_speed) override;

        // Configuration handlers: no fields of its own -- exists so this class
        // has its own real group() for the doc generator to find (see
        // tools/build_config_docs.py's SECTIONS table), matching PWM/Laser/
        // BESC/etc.'s own-file-per-type convention, rather than Dac silently
        // sharing OnOff's config_items.yaml section (or having none at all).
        void group(Configuration::HandlerBase& handler) override {
            // @default_for speed_map
            // @default 0=0% 10000=100%
            // @default_note applied by Dac::init() only when speed_map is left unset
            OnOff::group(handler);
        }

        ~Dac() {}

    private:
        bool _gpio_ok;  // DAC is on a valid pin

    protected:
        void set_output(uint32_t duty);  // sets DAC instead of PWM
    };
}
#endif
