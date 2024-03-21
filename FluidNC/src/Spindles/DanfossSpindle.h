// Copyright (c) 2024 - Jan Speckamp, whosmatt
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "VFDSpindle.h"

namespace Spindles {
    class DanfossVLT2800 : public VFD {
        union SpindleStatus {
            struct {
                bool control_ready : 1;   // bit 00 = 0: ""                        | Bit = 1: "Control ready"
                bool drive_ready : 1;     // bit 01 = 0: ""                        | Bit = 1: "Drive ready"
                bool coasting_stop : 1;   // bit 02 = 0: "Coasting stop"           | Bit = 1: ""
                bool trip : 1;            // bit 03 = 0: "No Trip"                 | Bit = 1: "Trip"
                bool unused1 : 1;         // bit 04 Not used
                bool unused2 : 1;         // bit 05 Not used
                bool trip_lock : 1;       // bit 06 = 0: ""                        | Bit = 1: "Trip lock"
                bool warning : 1;         // bit 07 = 0: "No warning"              | Bit = 1: "Warning"
                bool speed_status : 1;    // bit 08 = 0: "Speed != ref."           | Bit = 1: "Speed = ref."
                bool local_control : 1;   // bit 09 = 0: "Local control"           | Bit = 1: "Ser. communi."
                bool freq_range_err : 1;  // bit 10 = 0: "Outside frequency range" | Bit = 1: "Frequency limit OK"
                bool motor_running : 1;   // bit 11 = 0: ""                        | Bit = 1: "Motor running"
                bool unused3 : 1;         // bit 12 Not used
                bool voltage_warn : 1;    // bit 13 = 0: ""                        | Bit = 1: "Voltage warn."
                bool current_limit : 1;   // bit 14 = 0: ""                        | Bit = 1: "Current limit"
                bool thermal_warn : 1;    // bit 15 = 0: ""                        | Bit = 1: "Thermal wan."
            } flags;
            uint16_t statusWord;
        };

        union SpindleControl {
            struct {
                uint8_t reference_preset : 2;  // bit 00 = lsb of 2 bit value for preset reference selection
                                               // bit 01 = msb
                bool    dc_braking_stop : 1;   // bit 02 = 0 causes stop with dc brake
                bool    coasting_stop : 1;     // bit 03 = 0 causes coasting stop
                bool    quick_stop : 1;        // bit 04 = 0 causes quick stop
                bool    freeze_freq : 1;       // bit 05 = 0 causes output frequency to be locked from inputs, stops still apply
                bool    start_stop : 1;        // bit 06 = 1 causes motor start, 0 causes motor stop, standard ramp applies
                bool    reset : 1;             // bit 07 = resets trip condition on change from 0 to 1
                bool    jog : 1;               // bit 08 = 1 switches to jogging (par. 213)
                bool    ramp_select : 1;       // bit 09 = ramp selection: 0 = ramp 1 (par. 207-208), 1 = ramp 2 (par. 209-210)
                bool    data_valid : 1;        // bit 10 = 0 causes entire control word to be ignored
                bool    relay_01 : 1;          // bit 11 = 1 activates relay 01
                bool    output_46 : 1;         // bit 12 = 1 activates digital output on terminal 46
                uint8_t setup_preset : 2;      // bit 13 = lsb of 2 bit value for setup selection when par. 004 multi setup is enabled
                                               // bit 14 = msb
                bool reverse : 1;              // bit 15 = 1 causes reversing
            } flags;
            uint16_t controlWord;
        };

    protected:
        // Unlike other VFDs, the VLT seems to take speed as int16_t, mapped to 0-200% of the configured maximum reference.
        uint16_t _minFrequency = 0x0;     // motor off (0% speed)
        uint16_t _maxFrequency = 0x4000;  // max speed the VFD will allow. 0x4000 = 100% for VLT2800

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
        struct combinedSpindleState {
            SpindleState state;
            uint32_t     speed;
        } cachedSpindleState;

        void writeVFDState(combinedSpindleState spindle, ModbusCommand& data);

        void parse_spindle_status(uint16_t statusword, SpindleStatus& status);
    };
}
