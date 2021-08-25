// Copyright (c) 2020 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    H2ASpindle.cpp

    This is for the new H2A H2A VFD based spindle via RS485 Modbus.

                         WARNING!!!!
    VFDs are very dangerous. They have high voltages and are very powerful
    Remove power before changing bits.

    The documentation is okay once you get how it works, but unfortunately
    incomplete... See H2ASpindle.md for the remainder of the docs that I
    managed to piece together.
*/

#include "H2ASpindle.h"

namespace Spindles {
    H2A::H2A() : VFD() {}

    void H2A::direction_command(SpindleState mode, ModbusCommand& data) {
        data.tx_length = 6;
        data.rx_length = 6;

        data.msg[1] = 0x06;  // WRITE
        data.msg[2] = 0x20;  // Command ID 0x2000
        data.msg[3] = 0x00;
        data.msg[4] = 0x00;
        data.msg[5] = (mode == SpindleState::Ccw) ? 0x02 : (mode == SpindleState::Cw ? 0x01 : 0x06);
    }

    void H2A::set_speed_command(uint32_t dev_speed, ModbusCommand& data) {
        // For the H2A VFD, the speed is directly units of RPM, unlike many
        // other VFDs where it is given in Hz times some scale factor.
        data.tx_length = 6;
        data.rx_length = 6;

        data.msg[1] = 0x06;  // WRITE
        data.msg[2] = 0x10;  // Command ID 0x1000
        data.msg[3] = 0x00;
        data.msg[4] = dev_speed >> 8;
        data.msg[5] = dev_speed & 0xFF;
    }

    VFD::response_parser H2A::initialization_sequence(int index, ModbusCommand& data) {
        if (index == -1) {
            data.tx_length = 6;
            data.rx_length = 8;

            // Send: 01 03 B005 0002
            data.msg[1] = 0x03;  // READ
            data.msg[2] = 0xB0;  // B0.05 = Get RPM
            data.msg[3] = 0x05;
            data.msg[4] = 0x00;  // Read 2 values
            data.msg[5] = 0x02;

            //  Recv: 01 03 00 04 5D C0 03 F6
            //                    -- -- = 24000 (val #1)
            return [](const uint8_t* response, Spindles::VFD* vfd) -> bool {
                uint16_t maxRPM = (uint16_t(response[4]) << 8) | uint16_t(response[5]);

                if (vfd->_speeds.size() == 0) {
                    vfd->shelfSpeeds(maxRPM / 4, maxRPM);
                }

                vfd->setupSpeeds(1);  // The speed is given directly in RPM
                vfd->_slop = 300;     // 300 RPM

                log_info("H2A spindle initialized at " << maxRPM << " RPM");

                return true;
            };
        } else {
            return nullptr;
        }
    }

    VFD::response_parser H2A::get_current_speed(ModbusCommand& data) {
        data.tx_length = 6;
        data.rx_length = 8;

        // Send: 01 03 700C 0002
        data.msg[1] = 0x03;  // READ
        data.msg[2] = 0x70;  // B0.05 = Get speed
        data.msg[3] = 0x0C;
        data.msg[4] = 0x00;  // Read 2 values
        data.msg[5] = 0x02;

        //  Recv: 01 03 0004 095D 0000
        //                   ---- = 2397 (val #1)
        return [](const uint8_t* response, Spindles::VFD* vfd) -> bool {
            vfd->_sync_dev_speed = (uint16_t(response[4]) << 8) | uint16_t(response[5]);
            return true;
        };
    }

    VFD::response_parser H2A::get_current_direction(ModbusCommand& data) {
        data.tx_length = 6;
        data.rx_length = 6;

        // Send: 01 03 30 00 00 01
        data.msg[1] = 0x03;  // READ
        data.msg[2] = 0x30;  // Command group ID
        data.msg[3] = 0x00;
        data.msg[4] = 0x00;  // Message ID
        data.msg[5] = 0x01;

        // Receive: 01 03 00 02 00 02
        //                      ----- status

        // TODO: What are we going to do with this? Update vfd state?
        return [](const uint8_t* response, Spindles::VFD* vfd) -> bool { return true; };
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<H2A> registration("H2A");
    }
}
