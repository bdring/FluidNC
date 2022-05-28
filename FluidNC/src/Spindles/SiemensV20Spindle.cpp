// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2020 -	Stefan de Bruijn
// Copyright (c) 2022 -	Peter Newbery
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
STW1 -  Control word
Address = 40100 = 100 = 0x0063
+-----+----------------------------+--------------+---------------+--------------+---------------+-----------+
|  -  |             -              | Forward - ON | Forward - OFF | Reverse - ON | Reverse - OFF | Dissable? |
+-----+----------------------------+--------------+---------------+--------------+---------------+-----------+
| Bit | Signal name                | 0x0C7F       | 0x0C7E        | 0x047F       | 0x047E        | 0x0C3E    |
+-----+----------------------------+--------------+---------------+--------------+---------------+-----------+
| 0   | ON/OFF1                    | 1            | 0             | 1            | 0             | 0         |
| 1   | OFF2: Electr. stop         | 1            | 1             | 1            | 1             | 1         |
| 2   | OFF3: Fast stop            | 1            | 1             | 1            | 1             | 1         |
| 3   | Pulse enabled              | 1            | 1             | 1            | 1             | 1         |
| 4   | RFG enabled                | 1            | 1             | 1            | 1             | 1         |
| 5   | RFG start                  | 1            | 1             | 1            | 1             | 1         |
| 6   | Enable setpoint            | 1            | 1             | 1            | 1             | 0         |
| 7   | Error acknowledgement      | 0            | 0             | 0            | 0             | 0         |
| 8   | JOG right                  | 0            | 0             | 0            | 0             | 0         |
| 9   | JOG left                   | 0            | 0             | 0            | 0             | 0         |
| 10  | Controller of AG           | 1            | 1             | 1            | 1             | 1         |
| 11  | Reversing                  | 1            | 1             | 0            | 0             | 1         |
| 12  | -                          | 0            | 0             | 0            | 0             | 0         |
| 13  | Motor potentiometer higher | 0            | 0             | 0            | 0             | 0         |
| 14  | Motor potentiometer lower  | 0            | 0             | 0            | 0             | 0         |
| 15  | Manual/automatic mode      | 0            | 0             | 0            | 0             | 0         |
+-----+----------------------------+--------------+---------------+--------------+---------------+-----------+
Function Manual, 04/2018, FW V4.7 SP10, A5E34229197B AEprint3d

*/

#include "SiemensV20Spindle.h"

#include <algorithm>  // std::max

namespace Spindles {
    SiemensV20::SiemensV20() : VFD() {
        // Baud rate is set in the PD164 setting.  If it is not 9600, add, for example,
        // _baudrate = 19200;
    }

    void SiemensV20::direction_command(SpindleState mode, ModbusCommand& data) {
        // NOTE: data length is excluding the CRC16 checksum.
        data.tx_length = 6;
        data.rx_length = 6;

        // data.msg[0] is omitted (modbus address is filled in later)
        data.msg[1] = 0x06;
        data.msg[2] = 0x00;
        data.msg[3] = 0x63;

        switch (mode) {
            case SpindleState::Cw:
                data.msg[4] = 0x0C;
                data.msg[5] = 0x7F;
                break;
            case SpindleState::Ccw:
                data.msg[4] = 0x04;
                data.msg[5] = 0x7F;
                break;
            default:  // SpindleState::Disable
                data.msg[4] = 0x0C;
                data.msg[5] = 0x7E;
                break;
        }
    }

    void IRAM_ATTR SiemensV20::set_speed_command(uint32_t speed, ModbusCommand& data) {
        // The units for setting SiemensV20 speed are Hz * 100.  For a 2-pole motor,
        // RPM is Hz * 60 sec/min.  The maximum possible speed is 400 Hz so
        // 400 * 60 = 24000 RPM.
        
        log_warn("Setting VFD speed to " << speed);

        if (speed != 0 && (speed < _minFrequency || speed > _maxFrequency)) {
            log_warn(name() << " requested freq " << (speed) << " is outside of range (" << _minFrequency << "," << _maxFrequency << ")");
        }
        data.tx_length = 6;
        data.rx_length = 6;

        data.msg[1] = 0x06;
        data.msg[2] = 0x00;
        data.msg[3] = 0x64;
        data.msg[4] = speed >> 8;
        data.msg[5] = speed & 0xFF;
        /*
        // NOTE: data length is excluding the CRC16 checksum.
        data.tx_length = 6;
        data.rx_length = 6;

        // data.msg[0] is omitted (modbus address is filled in later)
        data.msg[1] = 0x06;
        data.msg[2] = 0x00;
        data.msg[3] = 0x64;

        uint16_t speed =  uint32_t(dev_speed) / 1.46484375;
        if (speed < 0) {
            speed = 0;
        }
        if (speed > 16384) {
            speed = 16384;
        }
        data.msg[4] = (speed >> 8) ;
        data.msg[5] = (speed & 0xFF);
        */
    }
	

    VFD::response_parser SiemensV20::get_current_speed(ModbusCommand& data) {
        // NOTE: data length is excluding the CRC16 checksum.
        data.tx_length = 6;
        data.rx_length = 5;

        // data.msg[0] is omitted (modbus address is filled in later)
        data.msg[1] = 0x03;  
        data.msg[2] = 0x00;  
        data.msg[3] = 0x6E;
        data.msg[4] = 0x00;  
        data.msg[5] = 0x01;


        return [](const uint8_t* response, Spindles::VFD* vfd) -> bool {
            uint16_t frequency = (response[4] << 8) | response[5];

            // Store speed for synchronization
            vfd->_sync_dev_speed = frequency;
            return true;
        };
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<SiemensV20> registration("SiemensV20");
    }
}
