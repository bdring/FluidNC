#pragma once

#include "VFDSpindle.h"

/*
    YL620Spindle.h

    Part of Grbl_ESP32
    2021 -  Marco Wagner

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

namespace Spindles {
    class YL620 : public VFD {
    protected:
        uint16_t _minFrequency = 0;     // frequency lower limit. Factor 10 of actual frequency
        uint16_t _maxFrequency = 4000;  // max frequency the VFD will allow. Normally 400.0. Factor 10 of actual frequency

        void direction_command(SpindleState mode, ModbusCommand& data) override;
        void set_speed_command(uint32_t rpm, ModbusCommand& data) override;

        response_parser initialization_sequence(int index, ModbusCommand& data) override;
        response_parser get_current_speed(ModbusCommand& data) override;
        response_parser get_current_direction(ModbusCommand& data) override;
        response_parser get_status_ok(ModbusCommand& data) override { return nullptr; }

        bool supports_actual_speed() const override { return true; }
        bool safety_polling() const override { return false; }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "YL620"; }

    public:
        YL620();
    };
}
