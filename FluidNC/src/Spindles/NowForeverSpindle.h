// Copyright (c) 2022 -	Lukas Goßmann (GitHub: LukasGossmann)
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "VFDSpindle.h"

namespace Spindles {
    class NowForever : public VFD {
    protected:
        uint16_t _minFrequency = 0;
        uint16_t _maxFrequency = 0;

        void updateRPM();

        void direction_command(SpindleState mode, ModbusCommand& data) override;
        void set_speed_command(uint32_t hz, ModbusCommand& data) override;

        response_parser initialization_sequence(int index, ModbusCommand& data) override;
        response_parser get_current_speed(ModbusCommand& data) override;
        response_parser get_current_direction(ModbusCommand& data) override;
        response_parser get_status_ok(ModbusCommand& data) override;
        bool            safety_polling() const { return true; }

        bool use_delay_settings() const override { return false; }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "NowForever"; }

    public:
        NowForever();
    };
}
