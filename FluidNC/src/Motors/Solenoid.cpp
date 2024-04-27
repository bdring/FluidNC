// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This lets an Solenoid act like an axis. It will active when the machine position of 
    the axis is above 0.0. This can be inverted with the direction_invert value.

    If inverted, it will active at below 0.0.

    When active the PWM will come on at the pull_percent value. After pull_ms time, it will change 
    to the hold_percent value. This can be used to keep the coil cooler.

    The feature runs on a 50ms update timer. The solenoid should react within 50ms of the position. 
    The pull_ms also used that 50ms update resolution.

    The PWM can be inverted using the :low attribute on the output pin. This inverts the signal in case
    you need it. It is not used to invert the direction logic. 

    The axis position still respects your speed and acceleration and other axis coordination. If you go
    from Z0 to Z5, it will activate as soon as it goes above 0. If you G0 from Z5 to Z0, it will not deactivate
    until it gets to Z0.  

    Example YAML

      solenoid:
        output_pin: gpio.26
        pwm_hz: 5000
        off_percent: 0.000
        pull_percent: 100.000
        hold_percent: 20.000
        pull_ms: 1000
        direction_invert: false

*/

#include "Solenoid.h"

#include "../Machine/MachineConfig.h"
#include "../System.h"      // mpos_to_steps() etc
#include "Driver/PwmPin.h"  // pwmInit(), etc.
#include "../Pin.h"
#include "../Limits.h"  // limitsMaxPosition

#include <freertos/task.h>  // vTaskDelay

namespace MotorDrivers {

    void Solenoid::init() {
        if (_output_pin.undefined()) {
            log_config_error("    Solenoid disabled: No output pin");
            _has_errors = true;
            return;  // We cannot continue without the output pin
        }

        _axis_index = axis_index();

        _pwm = new PwmPin(_output_pin, _pwm_freq);  // Allocate a channel

        pwm_cnt[SolenoidMode::Off]  = uint32_t(_off_percent / 100.0f * _pwm->period());
        pwm_cnt[SolenoidMode::Pull] = uint32_t(_pull_percent / 100.0f * _pwm->period());
        pwm_cnt[SolenoidMode::Hold] = uint32_t(_hold_percent / 100.0f * _pwm->period());

        config_message();

        _current_pwm_duty = 0;

        schedule_update(this, _update_rate_ms);
    }

    void Solenoid::update() { set_location(); }

    void Solenoid::config_message() {
        log_info("    " << name() << " Pin: " << _output_pin.name() << " Off: " << _off_percent << " Hold: " << _hold_percent << " Pull:"
                        << _pull_percent << " Duration:" << _pull_ms << " pwm hz:" << _pwm->frequency() << " period:" << _pwm->frequency());
    }

    void Solenoid::set_location() {
        bool is_solenoid_on;

        if (_has_errors) {
            return;
        }

        float mpos = steps_to_mpos(get_axis_motor_steps(_axis_index), _axis_index);  // get the axis machine position in mm

        _dir_invert ? is_solenoid_on = (mpos < 0.0) : is_solenoid_on = (mpos > 0.0);

        // TODO: we can apply an invert feature here if needed

        switch (_current_mode) {
            case SolenoidMode::Off:
                if (is_solenoid_on) {
                    _current_mode  = SolenoidMode::Pull;
                    _pull_off_time = _pull_ms / _update_rate_ms;
                }
                break;
            case SolenoidMode::Pull:
                if (is_solenoid_on) {  // count down
                    if (_pull_off_time == 0) {
                        _current_mode = SolenoidMode::Hold;
                        break;
                    }
                    _pull_off_time--;
                } else {  // turn off
                    _current_mode = SolenoidMode::Off;
                }
                break;
            case SolenoidMode::Hold:
                if (!is_solenoid_on) {
                    _current_mode = SolenoidMode::Off;
                }
                break;
            default:
                break;
        }

        _write_pwm(pwm_cnt[_current_mode]);
    }

    void Solenoid::set_disable(bool disable) {}  // NOP

    namespace {
        MotorFactory::InstanceBuilder<Solenoid> registration("solenoid");
    }
}
