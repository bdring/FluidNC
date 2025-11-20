// Copyright (c) 2021 -  Marco Wagner
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is for a Yalang YL620/YL620-A VFD based spindle to be controlled via RS485 Modbus RTU.

                         WARNING!!!!
    VFDs are very dangerous. They have high voltages and are very powerful
    Remove power before changing bits.

    =============================================================================================================
    A Chinese manual for Modbus communication to YL620 can be found at
    https://docs.google.com/document/d/1TkERAvHZby4uad_i9kSk19HlDhbM7_xd/edit
    You can use Google Translate to translate it.

    Only Modbus RTU mode is supported, not Modbus ASCII mode.

    Manual Configuration required for the YL620

    Parameter number        Description                     Value
    -------------------------------------------------------------------------------
    P00.00                  Main frequency                  400.00Hz (match to your spindle)
    P00.01                  Command source                  3
    
    P03.00                  RS485 Baud rate                 3 (9600)
    P03.01                  RS485 address                   1
    P03.02                  RS485 protocol                  2
    P03.08                  Frequency given lower limit     100.0Hz (match to your spindle cooling-type)

    ===============================================================================================================

    RS485 communication is standard Modbus RTU

    Therefore, the following operation codes are relevant:
    0x03:   read single holding register
    0x06:   write single holding register

    Given a parameter Pnn.mm, the high byte of the register address is nn,
    the low is mm.  The numbers nn and mm in the manual are given in decimal,
    so P13.16 would be register address 0x0d10 when represented in hex.

    Holding register address                Description
    ---------------------------------------------------------------------------
    0x0000                                  main frequency
    0x0308                                  frequency given lower limit

    0x2000                                  command register (further information below)
    0x2001                                  Modbus485 frequency command (x0.1Hz => 2500 = 250.0Hz)

    0x200A                                  Target frequency
    0x200B                                  Output frequency
    0x200C                                  Output current


    Command register at holding address 0x2000
    --------------------------------------------------------------------------
    bit 1:0             b00: No function
                        b01: shutdown command
                        b10: start command
                        b11: Jog command
    bit 3:2             reserved
    bit 5:4             b00: No function
                        b01: Forward command
                        b10: Reverse command
                        b11: change direction
    bit 7:6             b00: No function
                        b01: reset an error flag
                        b10: reset all error flags
                        b11: reserved
*/

#include "YL620Protocol.h"

#include "Spindles/VFDSpindle.h"

#include <algorithm>

namespace Spindles {
    namespace VFD {
        void YL620Protocol::direction_command(SpindleState mode, ModbusCommand& data) {
            data.tx_length = 6;
            data.rx_length = 6;

            // data.msg[0] is omitted (modbus address is filled in later)
            data.msg[1] = 0x06;  // 06: write output register
            data.msg[2] = 0x20;  // 0x2000: command register address
            data.msg[3] = 0x00;

            data.msg[4] = 0x00;  // High-Byte of command always 0x00
            switch (mode) {
                case SpindleState::Cw:
                    data.msg[5] = 0x12;  // Start in forward direction
                    break;
                case SpindleState::Ccw:
                    data.msg[5] = 0x22;  // Start in reverse direction
                    break;
                default:                 // SpindleState::Disable
                    data.msg[5] = 0x01;  // Disable spindle
                    break;
            }
        }

        void YL620Protocol::set_speed_command(uint32_t speed, ModbusCommand& data) {
#ifdef DEBUG_VFD
            log_debug("Setting VFD speed to " << speed);
#endif

            data.tx_length = 6;
            data.rx_length = 6;

            data.msg[1] = 0x06;
            data.msg[2] = 0x20;
            data.msg[3] = 0x01;
            data.msg[4] = speed >> 8;
            data.msg[5] = speed & 0xFF;
        }

        VFDProtocol::response_parser YL620Protocol::initialization_sequence(int index, ModbusCommand& data, VFDSpindle* vfd) {
            if (index == -1) {
                data.tx_length = 6;
                data.rx_length = 5;

                data.msg[1] = 0x03;
                data.msg[2] = 0x03;
                data.msg[3] = 0x08;
                data.msg[4] = 0x00;
                data.msg[5] = 0x01;

                //  Recv: 01 03 02 03 E8 xx xx
                //                 -- -- = 1000
                return [](const uint8_t* response, VFDSpindle* vfd, VFDProtocol* detail) -> bool {
                    auto yl620           = static_cast<YL620Protocol*>(detail);
                    yl620->_minFrequency = (uint16_t(response[3]) << 8) | uint16_t(response[4]);

#ifdef DEBUG_VFD
                    log_debug("YL620 allows minimum frequency of:" << yl620->_minFrequency << " Hz");
#endif

                    return true;
                };
            } else if (index == -2) {
                data.tx_length = 6;
                data.rx_length = 5;

                data.msg[1] = 0x03;
                data.msg[2] = 0x00;
                data.msg[3] = 0x00;
                data.msg[4] = 0x00;
                data.msg[5] = 0x01;

                //  Recv: 01 03 02 0F A0 xx xx
                //                 -- -- = 4000
                return [](const uint8_t* response, VFDSpindle* vfd, VFDProtocol* detail) -> bool {
                    auto yl620           = static_cast<YL620Protocol*>(detail);
                    yl620->_maxFrequency = (uint16_t(response[3]) << 8) | uint16_t(response[4]);

                    // frequency is in Hz * 10, so RPM is frequency * 60 / 10 = frequency * 6
                    // E.g. for 400 Hz, we have frequency = 4000, so 4000 * 6 = 24000 RPM

                    if (vfd->_speeds.size() == 0) {
                        // Convert from frequency in deciHz to RPM (*60/10)
                        SpindleSpeed maxRPM = yl620->_maxFrequency * 6;
                        SpindleSpeed minRPM = yl620->_minFrequency * 6;

                        vfd->shelfSpeeds(minRPM, maxRPM);
                    }

                    vfd->setupSpeeds(yl620->_maxFrequency);
                    vfd->_slop = std::max(int(yl620->_maxFrequency) / 40, 1);

                    //                vfd->_min_rpm = uint32_t(vfd->_max_rpm) * uint32_t(yl620->_minFrequency) /
                    //                                uint32_t(yl620->_maxFrequency);  //   1000 * 24000 / 4000 =   6000 RPM.

#ifdef DEBUG_VFD
                    log_debug("YL620 allows maximum frequency " << yl620->_maxFrequency << " Hz");
#endif

                    return true;
                };
            } else {
                return nullptr;
            }
        }

        VFDProtocol::response_parser YL620Protocol::get_current_speed(ModbusCommand& data) {
            data.tx_length = 6;
            data.rx_length = 5;

            // Send: 01 03 200B 0001
            data.msg[1] = 0x03;
            data.msg[2] = 0x20;
            data.msg[3] = 0x0B;
            data.msg[4] = 0x00;
            data.msg[5] = 0x01;

            //  Recv: 01 03 02 05 DC xx xx
            //                 ---- = 1500
            return [](const uint8_t* response, VFDSpindle* vfd, VFDProtocol* detail) -> bool {
                uint16_t freq = (uint16_t(response[3]) << 8) | uint16_t(response[4]);

                vfd->_sync_dev_speed = freq;
                return true;
            };
        }

        VFDProtocol::response_parser YL620Protocol::get_current_direction(ModbusCommand& data) {
            data.tx_length = 6;
            data.rx_length = 5;

            // Send: 01 03 20 00 00 01
            data.msg[1] = 0x03;
            data.msg[2] = 0x20;
            data.msg[3] = 0x00;
            data.msg[4] = 0x00;
            data.msg[5] = 0x01;

            // Receive: 01 03 02 00 0A xx xx
            //                   ----- status is in 00 0A bit 5:4

            // TODO: What are we going to do with this? Update vfd state?
            return [](const uint8_t* response, VFDSpindle* vfd, VFDProtocol* detail) -> bool { return true; };
        }

        // Configuration registration
        namespace {
            SpindleFactory::DependentInstanceBuilder<VFDSpindle, YL620Protocol> registration("YL620");
        }
    }
}
