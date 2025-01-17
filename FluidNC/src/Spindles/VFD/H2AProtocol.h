// Copyright (c) 2020 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "VFDProtocol.h"

namespace Spindles {
    namespace VFD {
        class H2AProtocol : public VFDProtocol {
        protected:
            void direction_command(SpindleState mode, ModbusCommand& data) override;
            void set_speed_command(uint32_t dev_speed, ModbusCommand& data) override;

            response_parser initialization_sequence(int index, ModbusCommand& data, VFDSpindle* vfd) override;
            response_parser get_current_speed(ModbusCommand& data) override;
            response_parser get_current_direction(ModbusCommand& data) override;
            response_parser get_status_ok(ModbusCommand& data) override { return nullptr; }

            bool use_delay_settings() const override { return false; }
            bool safety_polling() const override { return false; }

            uint32_t _maxRPM;
        };
    }
}
