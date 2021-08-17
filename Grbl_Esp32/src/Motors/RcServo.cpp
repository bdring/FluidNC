/*
    RcServo.cpp

    This allows an RcServo to be used like any other motor. Servos
    do have limitation in travel and speed, so you do need to respect that.

    Part of Grbl_ESP32

    2020 -	Bart Dring

    The servo's travel will be mapped against the axis with $X/MaxTravel

    The rotation can be inverted with by $Stepper/DirInvert

    Homing simply sets the axis Mpos to the endpoint as determined by $Homing/DirInvert

    Calibration is part of the setting (TBD) fixed at 1.00 now

    Grbl_ESP32 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    Grbl is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "RcServo.h"

#include "../Machine/MachineConfig.h"
#include "../Pins/LedcPin.h"
#include "../Pin.h"
#include "../Limits.h"  // limitsMaxPosition
#include "RcServoSettings.h"

#include <esp32-hal-ledc.h>  // ledcWrite
#include <freertos/task.h>   // vTaskDelay

namespace MotorDrivers {
    // RcServo::RcServo(Pin pwm_pin) : Servo(), _pwm_pin(pwm_pin) {}

    void RcServo::init() {
        _axis_index = axis_index();

        read_settings();
        _pwm_chan_num     = ledcInit(_pwm_pin, -1, SERVO_PULSE_FREQ, SERVO_PULSE_RES_BITS);  // Allocate a channel
        _current_pwm_duty = 0;

        _disabled = true;
        config_message();
        startUpdateTask(_timer_ms);
    }

    void RcServo::config_message() {
        log_info("    RC Servo Pin:" << _pwm_pin.name() << " Pulse Len(" << _pwm_pulse_min << "," << _pwm_pulse_max << ") " << axisLimits());
    }

    void RcServo::_write_pwm(uint32_t duty) {
        // to prevent excessive calls to ledcWrite, make sure duty has changed
        if (duty == _current_pwm_duty) {
            return;
        }

        _current_pwm_duty = duty;
        ledcSetDuty(_pwm_chan_num, duty);
    }

    // sets the PWM to zero. This allows most servos to be manually moved
    void IRAM_ATTR RcServo::set_disable(bool disable) {
        _disabled = disable;
        if (_disabled) {
            _write_pwm(0);
        }
    }

    // Homing justs sets the new system position and the servo will move there
    bool RcServo::set_homing_mode(bool isHoming) {
        auto axis                 = config->_axes->_axis[_axis_index];
        sys_position[_axis_index] = int32_t(axis->_homing->_mpos * axis->_stepsPerMm);  // convert to steps

        set_location();   // force the PWM to update now
        vTaskDelay(750);  // give time to move
        return false;     // Cannot be homed in the conventional way
    }

    void RcServo::update() { set_location(); }

    void RcServo::set_location() {
        uint32_t servo_pulse_len;
        float    servo_pos, mpos, offset;

        if (_disabled) {
            return;
        }

        read_settings();

        mpos = system_convert_axis_steps_to_mpos(sys_position, _axis_index);  // get the axis machine position in mm
        // TBD working in MPos
        offset    = 0;  // gc_state.coord_system[axis_index] + gc_state.coord_offset[axis_index];  // get the current axis work offset
        servo_pos = mpos - offset;  // determine the current work position

        // determine the pulse length
        servo_pulse_len = static_cast<uint32_t>(
            mapConstrain(servo_pos, limitsMinPosition(_axis_index), limitsMaxPosition(_axis_index), _pwm_pulse_min, _pwm_pulse_max));

        _write_pwm(servo_pulse_len);
    }

    void RcServo::read_settings() {
        if (_invert_direction) {
            // swap the pwm values
            _pwm_pulse_min = SERVO_MAX_PULSE * (1.0f + (1.0f - _cal_min));
            _pwm_pulse_max = SERVO_MIN_PULSE * (1.0f + (1.0f - _cal_max));

        } else {
            _pwm_pulse_min = SERVO_MIN_PULSE * _cal_min;
            _pwm_pulse_max = SERVO_MAX_PULSE * _cal_max;
        }
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<RcServo> registration("rc_servo");
    }
}
