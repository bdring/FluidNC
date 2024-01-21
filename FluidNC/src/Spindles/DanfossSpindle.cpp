// Copyright (c) 2024 -  Jan Speckamp
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is for a Danfoss VLT 2800 VFD based spindle to be controlled via RS485 Modbus RTU.

    Command Structure:
    ---- 
    Slave Address - <automatically set by fluidNC>
    Function - [READ_COIL | READ_HR]
    Starting Address HI - 
    Starting Address LO -  
    No of Points HI -
    No of Points LO -
    CRC - <automatically set by fluidNC>
*/

#include "DanfossSpindle.h"

#include <algorithm>

#define READ_COIL 0x01
#define READ_HR 0x03

#define WRITE_SINGLE_COIL 0x05
#define WRITE_MULTIPLE_COIL 0x0F

namespace Spindles {
    DanfossVLT2800::DanfossVLT2800() : VFD() {}

    VFD::response_parser DanfossVLT2800::initialization_sequence(int index, ModbusCommand& data) {

#ifdef DEBUG_VFD
        log_debug("Initializing DanfossVLT2800");
#endif

        // First step of initialization - Read out current status
        if (index == -1) {

            data.tx_length = 6; // including automatically set client_id, excluding crc
            data.rx_length = 5; // excluding crc

            // Read out current state
            data.msg[1] = READ_COIL;
            data.msg[2] = 0x00;
            data.msg[3] = 0x20; // Coil index 32
            data.msg[4] = 0x00;
            data.msg[5] = 0x10; // Read 16 Bits


            return [](const uint8_t* response, Spindles::VFD* vfd) -> bool {
            
                // const uint8_t slave_addr = response[0] 
                // const uint8_t function = response[1] 
                // const uint8_t response_byte_count = response[2] 
                const uint8_t data_1 = response[3]; // Data (Coils 40-33)
                const uint8_t data_2 = response[4]; // Data (Coils 48-41)

#ifdef DEBUG_VFD
                // Source: https://files.danfoss.com/download/Drives/MG10S202.pdf [Page 22]
                log_debug("Control ready:" << (data_1 >> 0) & 1);
                log_debug("Drive ready:" << (data_1 >> 1) & 1);
                log_debug("Coasting stop:" << (data_1 >> 2) & 1);
                log_debug("Trip status:" << (data_1 >> 3) & 1);

                log_debug("not used:" << (data_1 >> 4) & 1);
                log_debug("not used:" << (data_1 >> 5) & 1);
                log_debug("Trip lock:" << (data_1 >> 6) & 1);
                log_debug("No warning/warning:" << (data_1 >> 7) & 1);

                log_debug("Speed == ref:" << (data_2 >> 0) & 1);
                log_debug("Local operation/serial communication control:" << (data_2 >> 1) & 1);
                log_debug("Outside frequency range:" << (data_2 >> 2) & 1);
                log_debug("Motor running:" << (data_2 >> 3) & 1);

                log_debug("Not used:" << (data_2 >> 4) & 1);
                log_debug("Voltage warn:" << (data_2 >> 5) & 1);
                log_debug("Current limit:" << (data_2 >> 6) & 1);
                log_debug("Thermal warn:" << (data_2 >> 7) & 1);
#endif

                return true;
            };
        } else {
            return nullptr;
        }
    }

    void IRAM_ATTR DanfossVLT2800::set_speed_command(uint32_t speed, ModbusCommand& data) {
#ifdef DEBUG_VFD
        log_debug("Trying to set VFD speed to " << speed);
        log_debug("Ignoring set speed, hardcoding to 40% " << speed);
#endif

        data.tx_length = 11; // including automatically set client_id, excluding crc
        data.rx_length = 6;  // excluding crc

        // We write a full control word instead of setting individual coils
        data.msg[1] = WRITE_MULTIPLE_COIL;
        data.msg[2] = 0x00;
        data.msg[3] = 0x00; // start coil address
        data.msg[4] = 0x00;
        data.msg[5] = 0x20; // write length

        data.msg[6] = 0x04; // payload byte count

        // Start Command: 0000010001111100 = 047C HEX (reversed)
        data.msg[7] = 0x7C; 
        data.msg[8] = 0x04;

        // Speed Command: 4000 HEX = 100% speed
        // 40% of 4000 HEX = 1999 HEX(reversed)
        //TODO: replace this with actual input parameter
        data.msg[9] = 0x99; 
        data.msg[10] = 0x19;
    }

    void DanfossVLT2800::direction_command(SpindleState mode, ModbusCommand& data) {
#ifdef DEBUG_VFD
        log_debug("DanfossVLT2800::direction_command not implemented yet!");
#endif
    }

    VFD::response_parser DanfossVLT2800::get_current_speed(ModbusCommand& data) {
#ifdef DEBUG_VFD
        log_debug("DanfossVLT2800::get_current_speed not implemented yet!");
#endif
        return [](const uint8_t* response, Spindles::VFD* vfd) -> bool { return true; };;
    }

    VFD::response_parser DanfossVLT2800::get_current_direction(ModbusCommand& data) {
#ifdef DEBUG_VFD
        log_debug("DanfossVLT2800::get_current_direction not implemented yet!");
#endif
        return [](const uint8_t* response, Spindles::VFD* vfd) -> bool { return true; };;
    }

    namespace {
        SpindleFactory::InstanceBuilder<DanfossVLT2800> registration("DanfossVLT2800");
    }
}