// Copyright (c) 2022 -	Santiago Palomino
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "HBridgeSpindle.h"
#include "GCode.h"   // gc_state.modal
#include "System.h"  // sys

namespace Spindles {
    void HBridge::init() {
        is_reversable = _output_ccw_pin.defined();

        setupSpeeds(_pwm_freq);

        if (_output_cw_pin.defined()) {
            if (_output_cw_pin.capabilities().has(Pin::Capabilities::PWM)) {
                _output_cw_pin.setAttr(Pin::Attr::PWM, _pwm_freq);
            } else {
                log_error(name() << " output_cw_pin " << _output_cw_pin.name() << " cannot do PWM");
            }
        } else {
            log_error(name() << " output_cw_pin not defined");
        }

        if (_output_ccw_pin.defined()) {
            if (_output_ccw_pin.capabilities().has(Pin::Capabilities::PWM)) {
                _output_ccw_pin.setAttr(Pin::Attr::PWM, _pwm_freq);
            } else {
                log_error(name() << " output_ccw_pin " << _output_ccw_pin.name() << " cannot do PWM");
            }
        } else {
            log_error(name() << " output_ccw_pin not defined");
        }

        _current_state    = SpindleState::Disable;
        _current_pwm_duty = 0;
        _enable_pin.setAttr(Pin::Attr::Output);

        if (_speeds.size() == 0) {
            // The default speed map for a PWM spindle is linear from 0=0% to 10000=100%
            linearSpeeds(10000, 100.0f);
        }
        setupSpeeds(_output_cw_pin.maxDuty());
        init_atc();
        config_message();
    }

    void IRAM_ATTR HBridge::set_enable(bool enable) {
        if (_disable_with_zero_speed && sys.spindle_speed() == 0) {
            enable = false;
        }

        _enable_pin.synchronousWrite(enable);
    }

    void IRAM_ATTR HBridge::setSpeedfromISR(uint32_t dev_speed) {
        set_enable(gc_state.modal.spindle != SpindleState::Disable);
        set_output(dev_speed);
    }

    void HBridge::setState(SpindleState state, SpindleSpeed speed) {
        if (sys.abort()) {
            return;  // Block during abort.
        }

        if (!_output_cw_pin.defined() || !_output_ccw_pin.defined()) {
            log_config_error(name() << " spindle pins not defined");
        }

        uint32_t dev_speed = mapSpeed(state, speed);
        _state             = state;

        if (_current_state != state) {
            _current_state      = state;
            _duty_update_needed = true;
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
    void HBridge::config_message() {
        log_info(name() << " Spindle Ena:" << _enable_pin.name() << " Out CW:" << _output_cw_pin.name() << " Out CCW:"
                        << _output_ccw_pin.name() << " Freq:" << _pwm_freq << "Hz Period:" << _output_cw_pin.maxDuty() << atc_info()

        );
    }

    void IRAM_ATTR HBridge::set_output(uint32_t duty) {
        // to prevent excessive calls to setDuty, make sure duty has changed
        if (duty == _current_pwm_duty && !_duty_update_needed) {
            return;
        }

        _duty_update_needed = false;

        _current_pwm_duty = duty;

        if (_state == SpindleState::Cw) {
            _output_cw_pin.setDuty(0);
            _output_ccw_pin.setDuty(duty);
        } else if (_state == SpindleState::Ccw) {
            _output_cw_pin.setDuty(0);
            _output_ccw_pin.setDuty(duty);
        } else {  // M5
            _output_cw_pin.setDuty(0);
            _output_ccw_pin.setDuty(0);
        }
    }

    void HBridge::deinit() {
        stop();
        _output_cw_pin.setAttr(Pin::Attr::Input);
        _output_ccw_pin.setAttr(Pin::Attr::Input);
        _enable_pin.setAttr(Pin::Attr::Input);
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<HBridge> registration("HBridge");
    }
}
