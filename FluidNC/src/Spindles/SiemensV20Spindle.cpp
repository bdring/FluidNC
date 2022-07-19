// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2020 -	Stefan de Bruijn
// Copyright (c) 2022 -	Peter Newbery
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
STW1 -  Control word
Address = 40100 = 99 = 0x0063
+-----+----------------------------+--------------+---------------+--------------+---------------+-----------+
|  -  |             -              | Forward - ON | Forward - OFF | Reverse - ON | Reverse - OFF | Disable?  |
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

HSW - Speed Set point Register
16 bit signed number - scaled to 16384 - this depends on the max frequency set by the user on the VFD
Address = 40101 = 100 = 0x0064

HIW - Actual Speed
16 bit signed number - scaled to 16384 - this depends on the max frequency set by the user on the VFD
Address = 40111 = 110 = 0x006E

ZSW - Status Word
Address = 40110 = 109 = 0x006D
+-----+---------------------------------+-------------+
| Bit |              Name               |    Type     |
+-----+---------------------------------+-------------+
|   0 | Drive ready                     |             |
|   1 | Drive ready to run              |             |
|   2 | Drive running                   |             |
|   3 | Drive fault active              |             |
|   4 | OFF2 active                     | Low enabled |
|   5 | OFF3 active                     | Low enabled |
|   6 | ON inhibit active               |             |
|   7 | Drive warning active            |             |
|   8 | Deviation setpoint / act. value | Low enabled |
|   9 | PZD control                     |             |
|  10 | |fact|    P1082 (fmax)          |             |
|  11 | Warning: Motor current limit    | Low enabled |
|  12 | Motor holding brake active      |             |
|  13 | Motor overload                  | Low enabled |
|  14 | Motor runs right                |             |
|  15 | Inverter overload               | Low enabled |
+-----+---------------------------------+-------------+
SINAMICS V20 at S7-1200 via Modbus Entry-ID: 63696870, V1.2, 11/2014


VFD Settings:
To use this spindle type - it assumes you have a working/ configued VFD with motor - the following settings
are to change the method of which the VFD takes it information.
please do not enable this without a properly configured VFD


+-----------+------------------------------+-----------------+-------------------+---------+--------------------------------------------------+
| Parameter |         Description          | Factory default | Default for Cn011 | Set to: |                     Remarks                      |
+-----------+------------------------------+-----------------+-------------------+---------+--------------------------------------------------+
| P0700[0]  | Selection of command source  |               1 |                 5 |       5 | RS485 as the command source                      |
| P1000[0]  | Selection of frequency       |               1 |                 5 |       5 | RS485 as the speed setpoint                      |
| P2023[0]  | RS485 protocol selection     |               1 |                 2 |       2 | MODBUS RTU protocol                              |
| P2010[0]  | USS/MODBUS baudrate          |               6 |                 6 |       6 | Baudrate 9600 bps                                |
| P2021[0]  | MODBUS address               |               1 |                 1 |       1 | MODBUS address for inverter                      |
| P2022[0]  | MODBUS reply timeout         |            1000 |              1000 |    1000 | Maximum time to send reply back to the master    |
| P2014[0]  | USS/MODBUS telegram off time |            2000 |               100 |       0 | Time to receive data 0 = Disabled                |
| P2034     | MODBUS parity on RS485       |               2 |                 2 |       2 | Parity of MODBUS telegrams on RS485              |
| P2035     | MODBUS stop bits on RS485    |               1 |                 1 |       1 | Number of stop bits in MODBUS telegrams on RS485 |
+-----------+------------------------------+-----------------+-------------------+---------+--------------------------------------------------+
Operating Instructions, 09/2014, A5E34559884
Once the following setting have been set - you can then go ahead and enable connection macro - CN011

Machine spindle configuration:

SiemensV20:
  uart:
    txd_pin: gpio.17
    rxd_pin: gpio.16
    rts_pin: gpio.4
    baud: 9600
    mode: 8E1
  modbus_id: 1
  tool_num: 0
  speed_map: 0=0% 24000=100%

Take note that the serial interface use EVEN parity!

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
        

        log_debug("Setting VFD speed to " << uint32_t(speed));

        if (speed != 0 && (speed < _minFrequency || speed > _maxFrequency)) {
            log_warn(name() << " requested freq " << uint32_t(speed) << " is outside of range (" << _minFrequency << "," << _maxFrequency << ")");
        }
        /*
        V20 has a scalled input and is standardized to 16384 
        please note Signed numbers work IE -16384 to 16384 
        but for this implementation only posivite number are allowed
        */
        int16_t ScaledFreq = speed * _FreqScaler;
        log_debug("Setting VFD Scaled Value " << int16_t(ScaledFreq) << " Byte 1 " << uint8_t(ScaledFreq >> 8)  << " Byte 2 " << uint8_t(ScaledFreq & 0xFF));

        data.tx_length = 6;
        data.rx_length = 6;

        data.msg[1] = 0x06;
        data.msg[2] = 0x00;
        data.msg[3] = 0x64;
        data.msg[4] = ScaledFreq >> 8;
        data.msg[5] = ScaledFreq & 0xFF;

    }
    VFD::response_parser SiemensV20::initialization_sequence(int index, ModbusCommand& data) {
        /*
        The VFD does not have any noticeable registers to set this information up programmatically
        For now - it is user set in the software but is a typical setup
        */ 
        if (_minFrequency > _maxFrequency) {
            _minFrequency = _maxFrequency;
        }
        if (_speeds.size() == 0) {
            //RPM = (Frequency * (360/ Num_Phases))/Num_Poles
            SpindleSpeed minRPM = (_minFrequency * (360/ _NumberPhases)) / _numberPoles;
            SpindleSpeed maxRPM = (_maxFrequency * (360/ _NumberPhases)) / _numberPoles;
            shelfSpeeds(minRPM, maxRPM);
        }
        setupSpeeds(_maxFrequency);
        _slop = std::max(_maxFrequency / 40, 1);
        return nullptr;
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
            auto siemensV20           = static_cast<SiemensV20*>(vfd);
            int16_t Scaledfrequency = ((response[3] << 8) | response[4]);
            int16_t frequency = float(Scaledfrequency) / (-1* (siemensV20->_FreqScaler));
            log_debug("VFD Measured Value " << int16_t(Scaledfrequency) << " Freq " << int16_t(frequency));

            // Store speed for synchronization
            vfd->_sync_dev_speed = uint16_t(frequency);
            return true;
        };
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<SiemensV20> registration("SiemensV20");
    }
}
