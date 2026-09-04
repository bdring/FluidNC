// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "MotorDriver.h"

namespace MotorDrivers {
    class UnipolarMotor final : public MotorDriver {
    public:
        UnipolarMotor(const char* name) : MotorDriver(name) {}

        // Overrides for inherited methods
        void init() override;
        bool set_homing_mode(bool isHoming) override { return true; }
        bool can_self_home() override { return false; }
        void set_disable(bool disable) override;
        void set_direction(bool dir) override;
        void step() override;

        // The ISR path calls these directly, not through the vtable.  They are
        // non-virtual on purpose: whether a virtual call gets devirtualised is
        // the optimiser's choice, and a vtable read from the step ISR is a
        // flash access that panics the board when the cache is disabled.
        void step_isr();
        void set_direction_isr(bool dir);

        IsrStepFn isr_step_fn() override;
        IsrDirFn  isr_dir_fn() override;

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
            // True uses the 8-phase half-step sequence, which gives twice the resolution
            // and smoother motion than the 4-phase full-step sequence at some cost in
            // torque.  Setting it false selects full-step, which halves the number of
            // steps per revolution, so steps_per_mm must be halved to match.
            handler.item("half_step", _half_step);
        }

    private:
        Pin     _pin_phase0;
        Pin     _pin_phase1;
        Pin     _pin_phase2;
        Pin     _pin_phase3;
        uint8_t _current_phase = 0;
        bool    _half_step     = true;

        // Native GPIO numbers for the four phase pins, resolved in init().
        // step() writes them with gpio_write() rather than through Pin, whose
        // write() dispatches through a PinDetail vtable that lives in flash -
        // not safe to touch from the step ISR.  validate() has already required
        // these to be gpio pins.
        pinnum_t _gpio_phase[4]   = { INVALID_PINNUM, INVALID_PINNUM, INVALID_PINNUM, INVALID_PINNUM };
        bool     _invert_phase[4] = { false, false, false, false };
        bool     _enabled       = false;
        bool     _dir           = true;

    protected:
        void config_message() override;
    };
}
