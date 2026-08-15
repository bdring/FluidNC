// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Servo.h"
#include "Pin.h"

namespace MotorDrivers {
    // Base for a Servo driven by a single PWM output pin (RcServo, Solenoid).
    // Deliberately NOT part of Servo itself -- Dynamixel2 is also a Servo but
    // talks over UART, not a PWM pin, so it has no use for any of this; Servo's
    // own contract stays just update() plus the axis/disabled bookkeeping every
    // driver genuinely shares.
    class PwmServo : public Servo {
    protected:
        Pin      _output_pin;
        uint32_t _current_pwm_duty;

        void _write_pwm(uint32_t duty);

    public:
        PwmServo(const char* name) : Servo(name) {}
    };
}
