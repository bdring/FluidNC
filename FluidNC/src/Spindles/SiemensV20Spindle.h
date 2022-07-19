// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2020 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "VFDSpindle.h"

namespace Spindles {
    class SiemensV20 : public VFD {

    protected:
        uint16_t _minFrequency = 0;    // PD011: frequency lower limit. Normally 0.
        uint16_t _maxFrequency = 400;  // PD005: max frequency the VFD will allow. Normally 400.
        uint16_t _numberPoles  = 2; // 4 or 2 poles in motor. Default is 4. A spindle being 24000RPM@400Hz implies 2 poles
        uint16_t _NumberPhases  = 3; // Typically 3 Phases for standard VFDs
        float _FreqScaler = float(-16384.0) / _maxFrequency; // SPEED_SCALING Internally it is standardized to 16384. (16384 / 400) With this scaling HSW and HIW are transferred via the Modbus register.


        void direction_command(SpindleState mode, ModbusCommand& data) override;
        void set_speed_command(uint32_t rpm, ModbusCommand& data) override;
        response_parser initialization_sequence(int index, ModbusCommand& data) override;
        response_parser get_current_speed(ModbusCommand& data) override;
        response_parser get_status_ok(ModbusCommand& data) override { return nullptr; }

        bool use_delay_settings() const override { return false; }
        bool safety_polling() const override { return false; }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "SiemensV20"; }

    public:
        SiemensV20();
    };
}
