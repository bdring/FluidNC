/*
    UnipolarMotor.cpp

    Part of Grbl_ESP32

    2020 -	Bart Dring

    Grbl is free software: you can redistribute it and/or modify
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

#include "UnipolarMotor.h"
#include "../Machine/MachineConfig.h"

#include <esp_attr.h>  // IRAM_ATTR

namespace MotorDrivers {
    void UnipolarMotor::init() {
        _pin_phase0.setAttr(Pin::Attr::Output);
        _pin_phase1.setAttr(Pin::Attr::Output);
        _pin_phase2.setAttr(Pin::Attr::Output);
        _pin_phase3.setAttr(Pin::Attr::Output);
        _current_phase = 0;
        config_message();
    }

    void UnipolarMotor::config_message() {
        log_info("    Unipolar Stepper Ph0:" << _pin_phase0.name() << " Ph1:" << _pin_phase1.name() << " Ph2:" << _pin_phase2.name()
                                             << " Ph3:" << _pin_phase3.name() << " " << axisLimits());
    }

    void IRAM_ATTR UnipolarMotor::set_disable(bool disable) {
        if (disable) {
            _pin_phase0.off();
            _pin_phase1.off();
            _pin_phase2.off();
            _pin_phase3.off();
        }
        _enabled = !disable;
    }

    void IRAM_ATTR UnipolarMotor::set_direction(bool dir) { _dir = dir; }

    void IRAM_ATTR UnipolarMotor::step() {
        uint8_t _phase[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };  // temporary phase values...all start as off
        uint8_t phase_max;

        if (!_enabled) {
            return;  // don't do anything, phase is not changed or lost
        }

        phase_max = _half_step ? 7 : 3;

        if (_dir) {  // count up
            _current_phase = _current_phase == phase_max ? 0 : _current_phase + 1;
        } else {  // count down
            _current_phase = _current_phase == 0 ? phase_max : _current_phase - 1;
        }
        /*
			8 Step : A – AB – B – BC – C – CD – D – DA
			4 Step : AB – BC – CD – DA

			Step		IN4	IN3	IN2	IN1
			A 		0 	0 	0 	1
			AB		0	0	1	1
			B		0	0	1	0
			BC		0	1	1	0
			C		0	1	0	0
			CD		1	1	0	0
			D		1	0	0	0
			DA		1	0	0	1
		*/
        if (_half_step) {
            switch (_current_phase) {
                case 0:
                    _phase[0] = 1;
                    break;
                case 1:
                    _phase[0] = 1;
                    _phase[1] = 1;
                    break;
                case 2:
                    _phase[1] = 1;
                    break;
                case 3:
                    _phase[1] = 1;
                    _phase[2] = 1;
                    break;
                case 4:
                    _phase[2] = 1;
                    break;
                case 5:
                    _phase[2] = 1;
                    _phase[3] = 1;
                    break;
                case 6:
                    _phase[3] = 1;
                    break;
                case 7:
                    _phase[3] = 1;
                    _phase[0] = 1;
                    break;
            }
        } else {
            switch (_current_phase) {
                case 0:
                    _phase[0] = 1;
                    _phase[1] = 1;
                    break;
                case 1:
                    _phase[1] = 1;
                    _phase[2] = 1;
                    break;
                case 2:
                    _phase[2] = 1;
                    _phase[3] = 1;
                    break;
                case 3:
                    _phase[3] = 1;
                    _phase[0] = 1;
                    break;
            }
        }

        _pin_phase0.write(_phase[0]);
        _pin_phase1.write(_phase[1]);
        _pin_phase2.write(_phase[2]);
        _pin_phase3.write(_phase[3]);
        config->_stepping->startPulseTimer();
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<UnipolarMotor> registration("unipolar");
    }
}
