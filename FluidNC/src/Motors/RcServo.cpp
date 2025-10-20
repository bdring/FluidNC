// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This lets an RcServo be used like any other motor. Servos
    have travel and speed limitations that must be respected.

    The servo's travel will be mapped against the axis with $X/MaxTravel

    The rotation can be inverted by swapping the values
    min_pulse_us: 2100
    max_pulse_us: 1000

    Homing simply sets the axis Mpos to the endpoint as determined by homing/mpos

*/

#include "RcServo.h"

#include "Machine/MachineConfig.h"
#include "System.h"  // motor_pos_to_steps() etc
#include "Pin.h"
#include "RcServoSettings.h"

namespace MotorDrivers {
    void RcServo::init() {
        if (_output_pin.undefined()) {
            log_config_error("    RC Servo disabled: No output pin");
            _has_errors = true;
            return;  // We cannot continue without the output pin
        }

        _axis = axis_index();

        _output_pin.setAttr(Pin::Attr::PWM, _pwm_freq);

        _current_pwm_duty = 0;

        read_settings();

        config_message();

        _disabled = true;

        schedule_update(this, _timer_ms);
    }

    void RcServo::config_message() {
        log_info("    " << name() << " Pin:" << _output_pin.name() << " Pulse Len(" << _min_pulse_us << "," << _max_pulse_us
                        << " period:" << _output_pin.maxDuty() << ")");
    }

    void RcServo::_write_pwm(uint32_t duty) {
        // to prevent excessive calls to pwmSetDuty, make sure duty has changed
        if (duty == _current_pwm_duty) {
            return;
        }

        _current_pwm_duty = duty;
        _output_pin.setDuty(duty);
    }

    // sets the PWM to zero. This allows most servos to be manually moved
    void IRAM_ATTR RcServo::set_disable(bool disable) {
        //log_info("Set dsbl " << disable);
        if (_has_errors)
            return;

        _disabled = disable;
        if (_disabled) {
            _write_pwm(0);
        }
    }

    // Homing just sets the new system position and the servo will move there
    bool RcServo::set_homing_mode(bool isHoming) {
        if (_has_errors)
            return false;

        if (isHoming) {
            auto  axisConfig = Axes::_axis[_axis];
            auto  homing     = axisConfig->_homing;
            float motor_pos  = homing ? config->_kinematics->max_motor_pos(_axis) : 0;
            set_steps(_axis, motor_pos_to_steps(motor_pos, _axis));

            float home_time_sec = (axisConfig->_maxTravel / axisConfig->_maxRate * 60 * 1.1);  // 1.1 fudge factor for accell time.

            _disabled = false;
            set_location();                                         // force the PWM to update now
            dwell_ms(home_time_sec * 1000, DwellMode::SysSuspend);  // give time to move
        }
        return false;  // Cannot be homed in the conventional way
    }

    void RcServo::update() {
        set_location();
    }

    void RcServo::set_location() {
        if (_disabled || _has_errors) {
            return;
        }

        //        if (live_tuning()) {
        read_settings();
        //        }

        steps_t steps = get_axis_steps(_axis);  // get the axis machine position in mm

        // determine the pulse length
        uint32_t pulse_count = mapConstrain(steps, _min_steps, _max_steps, _min_pulse_cnt, _max_pulse_cnt);

        _write_pwm(pulse_count);

        // log_info("su " << servo_pulse_len);
    }

    void RcServo::read_settings() {
        uint32_t pulse_counts_per_ms = _pwm_freq * _output_pin.maxDuty() / 1000;

        _min_pulse_cnt = _min_pulse_us * pulse_counts_per_ms / 1000;
        _max_pulse_cnt = _max_pulse_us * pulse_counts_per_ms / 1000;

        _min_steps = motor_pos_to_steps(config->_kinematics->min_motor_pos(_axis), _axis);
        _max_steps = motor_pos_to_steps(config->_kinematics->max_motor_pos(_axis), _axis);
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<RcServo> registration("rc_servo");
    }
}
