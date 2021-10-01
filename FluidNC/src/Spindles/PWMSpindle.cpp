// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is a full featured TTL PWM spindle This does not include speed/power
    compensation. Use the Laser class for that.
*/
#include "PWMSpindle.h"

#include "../GCode.h"  // gc_state.modal
#include "../Logging.h"
#include "../Pins/LedcPin.h"
#include <esp32-hal-ledc.h>  // ledcDetachPin

// ======================= PWM ==============================
/*
    This gets called at startup or whenever a spindle setting changes
    If the spindle is running it will stop and need to be restarted with M3Snnnn
*/

namespace Spindles {
    void PWM::init() {
        if (_pwm_freq == 0) {
            log_error(name() << " PWM frequency is 0.");
            return;
        }

        get_pins_and_settings();
        setupSpeeds(_pwm_freq);

        if (_output_pin.undefined()) {
            log_warn(name() << " output pin not defined");
            return;  // We cannot continue without the output pin
        }

        if (!_output_pin.capabilities().has(Pin::Capabilities::PWM)) {
            log_warn(name() << " output pin " << _output_pin.name().c_str() << " cannot do PWM");
            return;
        }

        _current_state    = SpindleState::Disable;
        _current_pwm_duty = 0;

        auto outputNative = _output_pin.getNative(Pin::Capabilities::PWM);

        _pwm_chan_num = ledcInit(_output_pin, -1, (double)_pwm_freq, _pwm_precision);

        _enable_pin.setAttr(Pin::Attr::Output);
        _direction_pin.setAttr(Pin::Attr::Output);

        if (_speeds.size() == 0) {
            // The default speed map for a PWM spindle is linear from 0=0% to 10000=100%
            linearSpeeds(10000, 100.0f);
        }
        setupSpeeds(_pwm_period);
        config_message();
    }

    // Get the GPIO from the machine definition
    void PWM::get_pins_and_settings() {
        // setup all the pins

        is_reversable = _direction_pin.defined();

        _pwm_precision = calc_pwm_precision(_pwm_freq);  // determine the best precision
        _pwm_period    = (1 << _pwm_precision);
    }

    void IRAM_ATTR PWM::setSpeedfromISR(uint32_t dev_speed) {
        set_enable(gc_state.modal.spindle != SpindleState::Disable);
        set_output(dev_speed);
    }

    // XXX this is the same as OnOff::setState so it might be possible to combine them
    void PWM::setState(SpindleState state, SpindleSpeed speed) {
        if (sys.abort) {
            return;  // Block during abort.
        }

        // We always use mapSpeed() with the unmodified input speed so it sets
        // sys.spindle_speed correctly.
        uint32_t dev_speed = mapSpeed(speed);
        if (state == SpindleState::Disable) {  // Halt or set spindle direction and speed.
            if (_zero_speed_with_disable) {
                dev_speed = offSpeed();
            }
        } else {
            // XXX this could wreak havoc if the direction is changed without first
            // spinning down.
            set_direction(state == SpindleState::Cw);
        }

        // rate adjusted spindles (laser) in M4 set power via the stepper engine, not here

        // set_output must go first because of the way enable is used for level
        // converters on some boards.

        if (isRateAdjusted() && (state == SpindleState::Ccw)) {
            dev_speed = offSpeed();
            set_output(dev_speed);
        } else {
            set_output(dev_speed);
        }

        set_enable(state != SpindleState::Disable);
        spindleDelay(state, speed);
    }

    // prints the startup message of the spindle config
    void PWM::config_message() {
        log_info(name() << " Spindle Ena:" << _enable_pin.name() << " Out:" << _output_pin.name() << " Dir:" << _direction_pin.name()
                        << " Freq:" << _pwm_freq << "Hz Res:" << _pwm_precision << "bits"

        );
    }

    void IRAM_ATTR PWM::set_output(uint32_t duty) {
        if (_output_pin.undefined()) {
            return;
        }

        // to prevent excessive calls to ledcSetDuty, make sure duty has changed
        if (duty == _current_pwm_duty) {
            return;
        }

        _current_pwm_duty = duty;

        ledcSetDuty(_pwm_chan_num, duty);
    }

    /*
		Calculate the highest precision of a PWM based on the frequency in bits

		80,000,000 / freq = period
		determine the highest precision where (1 << precision) < period
	*/
    uint8_t PWM::calc_pwm_precision(uint32_t freq) {
        uint8_t precision = 0;
        if (freq == 0) {
            return precision;
        }

        // increase the precision (bits) until it exceeds allow by frequency the max or is 16
        while ((1u << precision) < uint32_t(80000000 / freq) && precision <= 16) {
            precision++;
        }

        return precision - 1;
    }

    void PWM::deinit() {
        stop();
        ledcDetachPin(_output_pin.getNative(Pin::Capabilities::PWM));
        _output_pin.setAttr(Pin::Attr::Input);
        _enable_pin.setAttr(Pin::Attr::Input);
        _direction_pin.setAttr(Pin::Attr::Input);
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<PWM> registration("PWM");
    }
}
