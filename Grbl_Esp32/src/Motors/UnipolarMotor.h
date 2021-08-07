#pragma once

/*
    UnipolarMotor.h

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

#include "MotorDriver.h"

namespace Motors {
    class UnipolarMotor : public Motor {
    public:
        UnipolarMotor() = default;

        // Overrides for inherited methods
        void init() override;
        bool set_homing_mode(bool isHoming) override { return true; }
        void set_disable(bool disable) override;
        void set_direction(bool) override;
        void step() override;

        // Configuration handlers:
        void validate() const override {
            Assert(!_pin_phase0.undefined(), "Phase 0 pin should be configured.");
            Assert(!_pin_phase1.undefined(), "Phase 1 pin should be configured.");
            Assert(!_pin_phase2.undefined(), "Phase 2 pin should be configured.");
            Assert(!_pin_phase3.undefined(), "Phase 3 pin should be configured.");
        }

        void group(Configuration::HandlerBase& handler) override {
            handler.item("phase0", _pin_phase0);
            handler.item("phase1", _pin_phase1);
            handler.item("phase2", _pin_phase2);
            handler.item("phase3", _pin_phase3);
            handler.item("half_step", _half_step);
        }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "unipolar"; }

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
