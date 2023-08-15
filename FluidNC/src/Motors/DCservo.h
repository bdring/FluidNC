// Copyright (c) 2023 -	Bar Smith
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
    For control of DC Servos
*/

#include "Servo.h"

namespace MotorDrivers {
    class DCservo : public Servo {

        public:
            DCservo();

            // Overrides for inherited methods
            void init() override;
            virtual bool set_homing_mode(bool isHoming) override { return false; } // implement set_homing_mode()
            virtual void update() override {} // implement update()

            // Name of the configurable. Must match the name registered in the cpp file.
            const char* name() const override { return "dc_servo"; }
            int encoderNumber;
        };
}
