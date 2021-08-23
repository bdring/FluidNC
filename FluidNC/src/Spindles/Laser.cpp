/*
    Laser.cpp

    This is similar to the PWM Spindle except that it allows the
    M4 speed vs. power copensation.

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
#include "Laser.h"

#include "../Machine/MachineConfig.h"

// ===================================== Laser ==============================================

namespace Spindles {
    bool Laser::isRateAdjusted() {
        return true;  // can use M4 (CCW) laser mode.
    }

    void Laser::config_message() {
        log_info(name() << " Spindle Ena:" << _enable_pin.name() << " Out:" << _output_pin.name() << " Freq:" << _pwm_freq
                        << "Hz Res:" << _pwm_precision << "bits Laser mode:" << (config->_laserMode ? "On" : "Off"));
    }

    // Get the GPIO from the machine definition
    void Laser::get_pins_and_settings() {
        // setup all the pins
        PWM::get_pins_and_settings();

        is_reversable = false;

        _pwm_precision = calc_pwm_precision(_pwm_freq);  // determine the best precision
        _pwm_period    = (1 << _pwm_precision);

        if (_speeds.size() == 0) {
            // The default speed map for a Laser is linear from 0=0% to 255=100%
            linearSpeeds(255, 100.0f);
        }

        setupSpeeds(_pwm_period);
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<Laser> registration("Laser");
    }
}
