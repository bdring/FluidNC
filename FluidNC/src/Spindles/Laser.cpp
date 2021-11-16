// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is similar to the PWM Spindle except that it enables the
    M4 speed vs. power compensation.
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
                        << "Hz Res:" << _pwm_precision << "bits Laser mode:On");
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
