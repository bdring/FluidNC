// Copyright (c) 2024 - Jan Speckamp
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "VFDSpindle.h"

namespace Spindles {
    class DanfossVLT2800 : public VFD {
        struct SpindleStatus {
            char control_ready;   // Bit = 0: ""                        | Bit = 1: "Control ready"
            char drive_ready;     // Bit = 0: ""                        | Bit = 1: "Drive ready"
            char coasting_stop;   // Bit = 0: "Coasting stop"           | Bit = 1: ""
            char trip;            // Bit = 0: "No Trip"                 | Bit = 1: "Trip"
            char trip_lock;       // Bit = 0: ""                        | Bit = 1: "Trip lock"
            char warning;         // Bit = 0: "No warning"              | Bit = 1: "Warning"
            char speed_status;    // Bit = 0: "Speed != ref."           | Bit = 1: "Speed = ref."
            char local_control;   // Bit = 0: "Local control"           | Bit = 1: "Ser. communi."
            char freq_range_err;  // Bit = 0: "Outside frequency range" | Bit = 1: "Frequency limit OK"
            char motor_running;   // Bit = 0: ""                        | Bit = 1: "Motor running"
            char voltage_warn;    // Bit = 0: ""                        | Bit = 1: "Voltage warn."
            char current_limit;   // Bit = 0: ""                        | Bit = 1: "Current limit"
            char thermal_warn;    // Bit = 0: ""                        | Bit = 1: "Thermal wan."
        };

    protected:
        uint16_t _minFrequency = 0;     // frequency lower limit. Factor 10 of actual frequency
        uint16_t _maxFrequency = 4000;  // max frequency the VFD will allow. Normally 400.0. Factor 10 of actual frequency

        void direction_command(SpindleState mode, ModbusCommand& data) override;
        void set_speed_command(uint32_t rpm, ModbusCommand& data) override;

        response_parser initialization_sequence(int index, ModbusCommand& data) { return nullptr; };
        response_parser get_current_speed(ModbusCommand& data) override;
        response_parser get_current_direction(ModbusCommand& data) override { return nullptr; };
        response_parser get_status_ok(ModbusCommand& data) override;

        bool safety_polling() const override { return true; }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "DanfossVLT2800"; }

    public:
        DanfossVLT2800();
        void init();

    private:
        void parse_spindle_status(uint16_t statusword, SpindleStatus& status);
    };

}
