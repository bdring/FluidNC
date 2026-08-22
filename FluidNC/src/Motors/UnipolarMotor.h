// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "MotorDriver.h"

namespace MotorDrivers {
    class UnipolarMotor : public MotorDriver {
    public:
        UnipolarMotor(const char* name) : MotorDriver(name) {}

        // Overrides for inherited methods
        void init() override;
        bool set_homing_mode(bool isHoming) override { return true; }
        bool can_self_home() override { return false; }
        void set_disable(bool disable) override;
        void set_direction(bool dir) override;
        void step() override;

        // Configuration handlers:
        void validate() override;

        void group(Configuration::HandlerBase& handler) override {
            // @config phase0_pin
            // @default NO_PIN
            // @pin_attributes output
            // Coil A drive output, e.g. IN1 on a ULN2003 board.
            handler.item("phase0_pin", _pin_phase0);

            // @config phase1_pin
            // @default NO_PIN
            // @pin_attributes output
            // Coil B drive output, e.g. IN2 on a ULN2003 board.
            handler.item("phase1_pin", _pin_phase1);

            // @config phase2_pin
            // @default NO_PIN
            // @pin_attributes output
            // Coil C drive output, e.g. IN3 on a ULN2003 board.
            handler.item("phase2_pin", _pin_phase2);

            // @config phase3_pin
            // @default NO_PIN
            // @pin_attributes output
            // Coil D drive output, e.g. IN4 on a ULN2003 board.
            handler.item("phase3_pin", _pin_phase3);

            // @config half_step
            // @default true
            // Uses the 8-phase half-step sequence, which gives twice the resolution and
            // smoother motion than the 4-phase full-step sequence at some cost in torque.
            // Halving this also halves the effective steps_per_mm.
            handler.item("half_step", _half_step);
        }

    private:
        Pin     _pin_phase0;
        Pin     _pin_phase1;
        Pin     _pin_phase2;
        Pin     _pin_phase3;
        uint8_t _current_phase = 0;
        bool    _half_step     = true;
        bool    _enabled       = false;
        bool    _dir           = true;

    protected:
        void config_message() override;
    };
}
