// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    DacSpindle.cpp

    This uses the Analog DAC in the ESP32 to generate a voltage
    proportional to the GCode S value desired. Some spindle uses
    a 0-5V or 0-10V value to control the spindle. You would use
    an Op Amp type circuit to get from the 0.3.3V of the ESP32 to that voltage.
*/
#include <sdkconfig.h>
#ifdef CONFIG_IDF_TARGET_ESP32

#    include "DacSpindle.h"

#    include <esp32-hal-dac.h>  // dacWrite

namespace Spindles {
    // ======================================== Dac ======================================
    void Dac::init() {
        if (_output_pin.undefined()) {
            return;
        }

        _gpio_ok = true;

        if (!_output_pin.capabilities().has(Pin::Capabilities::DAC)) {  // DAC can only be used on these pins
            _gpio_ok = false;
            log_error("DAC spindle pin invalid " << _output_pin.name().c_str() << " (pin 25 or 26 only)");
            return;
        }

        _direction_pin.setAttr(Pin::Attr::Output);

        is_reversable = _direction_pin.defined();

        if (_speeds.size() == 0) {
            linearSpeeds(10000, 100.0f);
        }
        setupSpeeds(255);

        config_message();
    }

    void Dac::config_message() {
        log_info(name() << " Spindle Out:" << _output_pin.name() << " Dir:" << _direction_pin.name() << " Res:8bits");
    }

    void IRAM_ATTR Dac::setSpeedfromISR(uint32_t speed) { set_output(speed); };
    void IRAM_ATTR Dac::set_output(uint32_t duty) {
        if (_gpio_ok) {
            auto outputNative = _output_pin.getNative(Pin::Capabilities::DAC);

            dacWrite(outputNative, static_cast<uint8_t>(duty));
        }
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<Dac> registration("DAC");
    }
}
#endif
