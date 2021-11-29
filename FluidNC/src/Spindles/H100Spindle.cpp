// Copyright (c) 2020 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    H100Spindle.cpp

    This is for a H100 VFD based spindle via RS485 Modbus.

                         WARNING!!!!
    VFDs are very dangerous. They have high voltages and are very powerful
    Remove power before changing bits.
*/

#include "H100Spindle.h"

#include <algorithm>   // std::max
#include <esp_attr.h>  // IRAM_ATTR

namespace Spindles {
    H100Spindle::H100Spindle() : VFD() {}

    void H100Spindle::direction_command(SpindleState mode, ModbusCommand& data) {
        // NOTE: data length is excluding the CRC16 checksum.
        data.tx_length = 6;
        data.rx_length = 6;

        // data.msg[0] is omitted (modbus address is filled in later)
        data.msg[1] = 0x05;
        data.msg[2] = 0x00;

        switch (mode) {
            case SpindleState::Cw:  //[01] [05] [00 49] [ff 00] -- forward run
                data.msg[3] = 0x49;
                data.msg[4] = 0xFF;
                data.msg[5] = 0x00;
                break;
            case SpindleState::Ccw:  //[01] [05] [00 4A] [ff 00] -- reverse run
                data.msg[3] = 0x4A;
                data.msg[4] = 0xFF;
                data.msg[5] = 0x00;
                break;
            default:  // SpindleState::Disable [01] [05] [00 4B] [ff 00] -- stop
                data.msg[3] = 0x4B;
                data.msg[4] = 0xFF;
                data.msg[5] = 0x00;
                break;
        }
    }

    void IRAM_ATTR H100Spindle::set_speed_command(uint32_t dev_speed, ModbusCommand& data) {
        data.tx_length = 6;
        data.rx_length = 6;

        if (dev_speed != 0 && (dev_speed < _minFrequency || dev_speed > _maxFrequency)) {
            log_warn(name() << " requested freq " << (dev_speed) << " is outside of range (" << _minFrequency << "," << _maxFrequency << ")");
        }

#ifdef DEBUG_VFD
        log_debug("Setting VFD dev_speed to " << dev_speed);
#endif

        //[01] [06] [0201] [07D0] Set frequency to [07D0] = 200.0 Hz. (2000 is written!)

        // data.msg[0] is omitted (modbus address is filled in later)
        data.msg[1] = 0x06;  // Set register command
        data.msg[2] = 0x02;
        data.msg[3] = 0x01;
        //data.msg[4] = dev_speed >> 8; - BC 11/24/21
        data.msg[4] = dev_speed >> 8;
        data.msg[5] = dev_speed & 0xFF;
    }

    // This gets data from the VFD. It does not set any values
    VFD::response_parser H100Spindle::initialization_sequence(int index, ModbusCommand& data) {
        // NOTE: data length is excluding the CRC16 checksum.
        data.tx_length = 6;
        data.rx_length = 5;
        // Read F011 (min frequency) and F005 (max frequency):
        //
        // [03] [000B] [0001] gives [03] [02] [xxxx] (with 02 the result byte count).
        // [03] [0005] [0001] gives [03] [02] [xxxx] (with 02 the result byte count).

        // data.msg[0] is omitted (modbus address is filled in later)
        data.msg[1] = 0x03;  // Read setting
        data.msg[2] = 0x00;
        //      [3] = set below...
        data.msg[4] = 0x00;  // length
        data.msg[5] = 0x01;

        if (index == -1) {
            // Max frequency
            data.msg[3] = 0x05;  // PD005: max frequency the VFD will allow. Normally 400.

            return [](const uint8_t* response, Spindles::VFD* vfd) -> bool {
                uint16_t value = (response[3] << 8) | response[4];

#ifdef DEBUG_VFD
                log_debug("VFD: Max frequency = " << value / 10 << "Hz " << value / 10 * 60 << "RPM");
#endif
                log_info("VFD: Max speed:" << (value / 10 * 60) << "rpm");

                // Set current RPM value? Somewhere?
                auto h100           = static_cast<H100Spindle*>(vfd);
                h100->_maxFrequency = value;

                return true;
            };

        } else if (index == -2) {
            // Min Frequency
            data.msg[3] = 0x0B;  // PD011: frequency lower limit. Normally 0.

            return [](const uint8_t* response, Spindles::VFD* vfd) -> bool {
                uint16_t value = (response[3] << 8) | response[4];

#ifdef DEBUG_VFD
                log_debug("VFD: Min frequency = " << value / 10 << "Hz " << value / 10 * 60 << "RPM");
#endif
                log_info("VFD: Min speed:" << (value / 10 * 60) << "rpm");

                // Set current RPM value? Somewhere?
                auto h100           = static_cast<H100Spindle*>(vfd);
                h100->_minFrequency = value;

                h100->updateRPM();

                return true;
            };
        }

        // Done.
        return nullptr;
    }

    void H100Spindle::updateRPM() {
        if (_minFrequency > _maxFrequency) {
            _minFrequency = _maxFrequency;
        }

        if (_speeds.size() == 0) {
            SpindleSpeed minRPM = _minFrequency * 60 / 10;
            SpindleSpeed maxRPM = _maxFrequency * 60 / 10;

            shelfSpeeds(minRPM, maxRPM);
        }
        setupSpeeds(_maxFrequency);
        _slop = std::max(_maxFrequency / 40, 1);

        log_info("VFD: VFD settings read: Freq range(" << _minFrequency << " , " << _maxFrequency << ")]");
    }

    VFD::response_parser H100Spindle::get_current_speed(ModbusCommand& data) {
        // NOTE: data length is excluding the CRC16 checksum.
        // [01] [04] [0000] [0002] -- output frequency
        data.tx_length = 6;
        data.rx_length = 7;

        // data.msg[0] is omitted (modbus address is filled in later)
        data.msg[1] = 0x04;
        data.msg[2] = 0x00;
        data.msg[3] = 0x00;  // Output frequency
        data.msg[4] = 0x00;
        data.msg[5] = 0x02;

        return [](const uint8_t* response, Spindles::VFD* vfd) -> bool {
            // 01 04 04 [freq 16] [set freq 16] [crc16]
            uint16_t frequency = (uint16_t(response[3]) << 8) | uint16_t(response[4]);

            // Store speed for synchronization
            vfd->_sync_dev_speed = frequency;
            return true;
        };
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<H100Spindle> registration("H100");
    }
}
