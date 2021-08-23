#pragma once

/*
    HuanyangSpindle.h

    Part of Grbl_ESP32
    2020 -  Bart Dring
    2020 -  Stefan de Bruijn

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

#include "VFDSpindle.h"

namespace Spindles {
    class Huanyang : public VFD {
    private:
        int reg;

    protected:
        uint16_t _minFrequency = 0;    // PD011: frequency lower limit. Normally 0.
        uint16_t _maxFrequency = 400;  // PD005: max frequency the VFD will allow. Normally 400.
        uint16_t _maxRpmAt50Hz = 100;  // PD144: rated motor revolution at 50Hz => 24000@400Hz = 3000@50HZ
        // uint16_t _numberPoles  = 2; // PD143: 4 or 2 poles in motor. Default is 4. A spindle being 24000RPM@400Hz implies 2 poles

        void updateRPM();

        void direction_command(SpindleState mode, ModbusCommand& data) override;
        void set_speed_command(uint32_t rpm, ModbusCommand& data) override;

        response_parser initialization_sequence(int index, ModbusCommand& data) override;
        response_parser get_status_ok(ModbusCommand& data) override;
        response_parser get_current_speed(ModbusCommand& data) override;

        bool supports_actual_speed() const override { return true; }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "Huanyang"; }

    public:
        Huanyang();
    };
}
