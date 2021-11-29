// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    10vSpindle.cpp

    This is basically a PWM spindle with some changes, so a separate forward and
    reverse signal can be sent.

    The direction pins will act as enables for the 2 directions. There is usually
    a min RPM with VFDs, that speed will remain even if speed is 0. You
    must turn off both direction pins when enable is off.
*/

#include "10vSpindle.h"

#include "../Pins/LedcPin.h"
#include "../System.h"       // sys.spindle_speed
#include "../GCode.h"        // gc_state.modal
#include <esp32-hal-ledc.h>  // ledcDetachPin

namespace Spindles {
    void _10v::init() {
        get_pins_and_settings();

        // a couple more pins not inherited from PWM Spindle
        if (_output_pin.undefined()) {
            log_warn("Spindle output pin not defined");
            return;  // We cannot continue without the output pin
        }

        _pwm_chan_num = ledcInit(_output_pin, -1, (double)_pwm_freq, _pwm_precision);  // allocate and setup a PWM channel

        _enable_pin.setAttr(Pin::Attr::Output);
        _direction_pin.setAttr(Pin::Attr::Output);
        _forward_pin.setAttr(Pin::Attr::Output);
        _reverse_pin.setAttr(Pin::Attr::Output);

        if (_speeds.size() == 0) {
            shelfSpeeds(6000, 20000);
        }

        // We set the dev_speed scale in the speed map to the full PWM period (64K)
        // Then, in set_output, we map the dev_speed range of 0..64K to the pulse
        // length range of ~1ms .. 2ms
        setupSpeeds(_pwm_period);

        stop();

        config_message();

        is_reversable = true;  // these VFDs are always reversable
    }

    // prints the startup message of the spindle config
    void _10v::config_message() {
        log_info(name() << " Spindle Ena:" << _enable_pin.name() << " Out:" << _output_pin.name() << " Dir:" << _direction_pin.name()
                        << " Fwd:" << _forward_pin.name() << " Rev:" << _reverse_pin.name() << " Freq:" << _pwm_freq
                        << "Hz Res:" << _pwm_precision << "bits");
    }

    // This appears identical to the code in PWMSpindle.cpp but
    // it uses the 10v versions of set_enable and set_output
    void IRAM_ATTR _10v::setSpeedfromISR(uint32_t dev_speed) {
        set_enable(gc_state.modal.spindle != SpindleState::Disable);
        set_output(dev_speed);
    }

    void IRAM_ATTR _10v::set_enable(bool enable) {
        if (_disable_with_zero_speed && sys.spindle_speed == 0) {
            enable = false;
        }

        _enable_pin.synchronousWrite(enable);

        // turn off anything that acts like an enable
        if (!enable) {
            _direction_pin.synchronousWrite(enable);
            _forward_pin.synchronousWrite(enable);
            _reverse_pin.synchronousWrite(enable);
        }
    }

    void _10v::set_direction(bool Clockwise) {
        _direction_pin.synchronousWrite(Clockwise);
        _forward_pin.synchronousWrite(Clockwise);
        _reverse_pin.synchronousWrite(!Clockwise);
    }

    void _10v::deinit() {
        _enable_pin.setAttr(Pin::Attr::Input);
        _direction_pin.setAttr(Pin::Attr::Input);
        _forward_pin.setAttr(Pin::Attr::Input);
        _reverse_pin.setAttr(Pin::Attr::Input);
        ledcDetachPin(_output_pin.getNative(Pin::Capabilities::PWM));
        _output_pin.setAttr(Pin::Attr::Input);
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<_10v> registration("10V");
    }
}
