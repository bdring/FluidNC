// Copyright (c) 2021 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
    Stepstick.cpp -- stepstick type stepper drivers
*/

#include "StandardStepper.h"

namespace MotorDrivers {
    class StepStick : public StandardStepper {
        Pin _MS1;
        Pin _MS2;
        Pin _MS3;
        Pin _Reset;

    public:
        StepStick(const char* name) : StandardStepper(name) {}

        void init() override;

        // Configuration handlers:
        void validate() override;
        void group(Configuration::HandlerBase& handler) override;

        void afterParse() override;

        ~StepStick() = default;
    };
}
