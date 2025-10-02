// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2020 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "VFDProtocol.h"

namespace Spindles {
    namespace VFD {
        class HuanyangProtocol : public VFDProtocol {
        private:
            uint8_t reg = 0;

        protected:
            uint16_t _minFrequency = 0;    // PD011: frequency lower limit. Normally 0.
            uint16_t _maxFrequency = 400;  // PD005: max frequency the VFD will allow. Normally 400.
            uint16_t _maxRpmAt50Hz = 100;  // PD144: rated motor revolution at 50Hz => 24000@400Hz = 3000@50HZ
            uint16_t _numberPoles  = 2;    // PD143: 4 or 2 poles in motor. Default is 4. A spindle being 24000RPM@400Hz implies 2 poles

            void updateRPM(VFDSpindle* vfd);

            void direction_command(SpindleState mode, ModbusCommand& data) override;
            void set_speed_command(uint32_t rpm, ModbusCommand& data) override;

            response_parser initialization_sequence(int index, ModbusCommand& data, VFDSpindle* vfd) override;
            response_parser get_status_ok(ModbusCommand& data) override;
            response_parser get_current_speed(ModbusCommand& data) override;
        };
    }
}
