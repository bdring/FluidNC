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
        log_info("Initializing DanfossVLT2800");
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
                const uint8_t data10 = data_1 >> 0 & 1;
                const uint8_t data11 = data_1 >> 1 & 1;
                const uint8_t data12 = data_1 >> 2 & 1;
                const uint8_t data13 = data_1 >> 3 & 1;
                const uint8_t data14 = data_1 >> 4 & 1;
                const uint8_t data15 = data_1 >> 5 & 1;
                const uint8_t data16 = data_1 >> 6 & 1;
                const uint8_t data17 = data_1 >> 7 & 1;
                
                const uint8_t data20 = data_2 >> 0 & 1;
                const uint8_t data21 = data_2 >> 1 & 1;
                const uint8_t data22 = data_2 >> 2 & 1;
                const uint8_t data23 = data_2 >> 3 & 1;
                const uint8_t data24 = data_2 >> 4 & 1;
                const uint8_t data25 = data_2 >> 5 & 1;
                const uint8_t data26 = data_2 >> 6 & 1;
                const uint8_t data27 = data_2 >> 7 & 1;

                // Source: https://files.danfoss.com/download/Drives/MG10S202.pdf [Page 22]
                log_info("Control ready:" << data10);
                log_info("Control ready:" << data11);
                log_info("Drive ready:" << data12);
                log_info("Coasting stop:" << data13);
                log_info("Trip status:" << data14);

                log_info("Trip lock:" << data16);
                log_info("No warning/warning:" << data17);

                log_info("Speed == ref:" << data20);
                log_info("Local operation/serial communication control:" << data21);
                log_info("Outside frequency range:" << data22);
                log_info("Motor running:" << data23);

                log_info("Not used:" << data24);
                log_info("Voltage warn:" << data25);
                log_info("Current limit:" << data26);
                log_info("Thermal warn:" <<data27);
#endif

                return true;
            };
        } else {
            return nullptr;
        }
    }

    void IRAM_ATTR DanfossVLT2800::set_speed_command(uint32_t dev_speed, ModbusCommand& data) {
        // TODO
#ifdef DEBUG_VFD
        log_info("Trying to set VFD speed to " << dev_speed);
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
        data.msg[9] = dev_speed >> 8;
        data.msg[10] = dev_speed & 0xFF;
    }

    void DanfossVLT2800::direction_command(SpindleState mode, ModbusCommand& data) {
#ifdef DEBUG_VFD
        log_info("DanfossVLT2800::direction_command not implemented yet!");
#endif
// TODO: this does not work as fluidNC adds CRC automatically to TX!!
        data.tx_length = 0; // including automatically set client_id, excluding crc
        data.rx_length = 0;  // excluding crc
    }

    VFD::response_parser DanfossVLT2800::get_current_speed(ModbusCommand& data) {
        data.tx_length = 6; // including automatically set client_id, excluding crc
        data.rx_length = 3 + 2;  // excluding crc

        // We write a full control word instead of setting individual coils
        data.msg[1] = READ_HR;
        data.msg[2] = 0x14;
        data.msg[3] = 0x3b; // start register
        data.msg[4] = 0x00;
        data.msg[5] = 0x01; // no of points

        return [](const uint8_t* response, Spindles::VFD* vfd) -> bool {
            
                // const uint8_t slave_addr = response[0] 
                // const uint8_t function = response[1] 
                // const uint8_t response_byte_count = response[2] 

                uint16_t freq = (uint16_t(response[3]) << 8) | uint16_t(response[4]);
                log_info("current frequency: " << freq);
                vfd->_sync_dev_speed = freq;
                return true;
         };
    }

    VFD::response_parser DanfossVLT2800::get_current_direction(ModbusCommand& data) {
#ifdef DEBUG_VFD
        log_info("DanfossVLT2800::get_current_direction not implemented yet!");
#endif
        return [](const uint8_t* response, Spindles::VFD* vfd) -> bool { return true; };;
    }

    namespace {
        SpindleFactory::InstanceBuilder<DanfossVLT2800> registration("DanfossVLT2800");
    }
}