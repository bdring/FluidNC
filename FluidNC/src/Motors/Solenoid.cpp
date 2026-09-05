// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This lets an Solenoid act like an axis. It will active when the machine position of 
    the axis is above 0.0. This can be inverted with the direction_invert value.

    If inverted, it will active at below 0.0.

    When active the PWM will come on at the pull_percent value. After pull_ms time, it will change 
    to the hold_percent value. This can be used to keep the coil cooler.

    The feature runs on a timer_ms update timer (50ms by default). The solenoid should react
    within timer_ms of the position. pull_ms is also measured in units of that same update
    resolution (pull_ms / timer_ms update ticks).

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

#include "Machine/MachineConfig.h"
#include "System.h"         // motor_pos_to_steps() etc
#include "Driver/PwmPin.h"  // pwmInit(), etc.
#include "Pin.h"

namespace MotorDrivers {

    void Solenoid::init() {
        if (_output_pin.undefined()) {
            log_config_error("    Solenoid disabled: No output pin");
            _has_errors = true;
            return;  // We cannot continue without the output pin
        }

        _axis = axis_index();

        _output_pin.setAttr(Pin::Attr::PWM, _pwm_freq);

        auto max_duty               = _output_pin.maxDuty();
        pwm_cnt[SolenoidMode::Off]  = uint32_t(_off_percent * max_duty / 100.0f);
        pwm_cnt[SolenoidMode::Pull] = uint32_t(_pull_percent * max_duty / 100.0f);
        pwm_cnt[SolenoidMode::Hold] = uint32_t(_hold_percent * max_duty / 100.0f);

        config_message();

        _current_pwm_duty = 0;

        schedule_update(this, _timer_ms);
    }

    void Solenoid::update() {
        set_location();
    }

    bool Solenoid::set_homing_mode(bool isHoming) {
        if (_has_errors) {
            return false;
        }

        if (isHoming) {
            auto  axisConfig = Axes::_axis[_axis];
            auto  homing     = axisConfig->_homing;
            float motor_pos  = homing ? config->_kinematics->max_motor_pos(_axis) : 0;
            set_steps(_axis, motor_pos_to_steps(motor_pos, _axis));

            float home_time_sec = (axisConfig->_maxTravel / axisConfig->_maxRate * 60 * 1.1);  // 1.1 fudge factor for accell time.

            set_location();                                        // force the solenoid state to update now
            dwell_ms(home_time_sec * 1000, DwellMode::SysSuspend);  // give time to move
        }
        return false;  // Cannot be homed in the conventional way
    }

    void Solenoid::config_message() {
        log_info("    " << name() << " Pin: " << _output_pin.name() << " Off: " << _off_percent << " Hold: " << _hold_percent << " Pull:"
                        << _pull_percent << " Duration:" << _pull_ms << " pwm hz:" << _pwm_freq << " period:" << _output_pin.maxDuty());
    }

    void Solenoid::set_location() {
        bool is_solenoid_on;

        if (_has_errors) {
            return;
        }

        float mpos = steps_to_motor_pos(get_axis_steps(_axis), _axis);  // get the axis machine position in mm

        _dir_invert ? is_solenoid_on = (mpos < 0.0) : is_solenoid_on = (mpos > 0.0);

        // TODO: we can apply an invert feature here if needed

        switch (_current_mode) {
            case SolenoidMode::Off:
                if (is_solenoid_on) {
                    _current_mode  = SolenoidMode::Pull;
                    _pull_off_time = _pull_ms / _timer_ms;
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
