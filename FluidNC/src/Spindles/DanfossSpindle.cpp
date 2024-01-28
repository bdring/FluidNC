// Copyright (c) 2024 -  Jan Speckamp, whosmatt, Jan 'jarainf' Rathner
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is for a Danfoss VLT 2800 VFD based spindle to be controlled via RS485 Modbus RTU.
*/

#include "DanfossSpindle.h"

#include <algorithm>

#define READ_COIL 0x01
#define READ_HR 0x03

#define WRITE_SINGLE_COIL 0x05
#define WRITE_MULTIPLE_COIL 0x0F

namespace Spindles {
    DanfossVLT2800::DanfossVLT2800() : VFD() {}

    void DanfossVLT2800::init() {
        VFD::init();
        is_reversable = false;
        setupSpeeds(_maxFrequency);
    }

    void IRAM_ATTR DanfossVLT2800::set_speed_command(uint32_t dev_speed, ModbusCommand& data) {
        // TODO
#ifdef DEBUG_VFD
        log_debug("Trying to set VFD speed to " << dev_speed);
#endif

        data.tx_length = 11;  // including automatically set client_id, excluding crc
        data.rx_length = 6;   // excluding crc

        // We write a full control word instead of setting individual coils
        data.msg[1] = WRITE_MULTIPLE_COIL;
        data.msg[2] = 0x00;
        data.msg[3] = 0x00;  // start coil address
        data.msg[4] = 0x00;
        data.msg[5] = 0x20;  // write length

        data.msg[6] = 0x04;  // payload byte count

        // Start Command: 0000010001111100 = 047C HEX (reversed)
        data.msg[7] = 0x7C;
        data.msg[8] = 0x04;

        // Speed Command: 4000 HEX = 100% speed
        // 40% of 4000 HEX = 1999 HEX(reversed)
        data.msg[10]  = dev_speed >> 8;
        data.msg[9] = dev_speed & 0xFF;
    }

    void DanfossVLT2800::direction_command(SpindleState mode, ModbusCommand& data) {
#ifdef DEBUG_VFD
        log_debug("DanfossVLT2800::direction_command not implemented yet!");
#endif
        // fluidNC does not currently allow defining a VFDSpindle without direction command and data is always sent
        // send valid request to avoid error
        get_current_speed(data); 
    }

    VFD::response_parser DanfossVLT2800::get_current_speed(ModbusCommand& data) {
        data.tx_length = 6;      // including automatically set client_id, excluding crc
        data.rx_length = 3 + 2;  // excluding crc

        // We write a full control word instead of setting individual coils
        data.msg[1] = READ_HR;
        data.msg[2] = 0x14;
        data.msg[3] = 0x3b;  // start register
        data.msg[4] = 0x00;
        data.msg[5] = 0x01;  // no of points

        return [](const uint8_t* response, Spindles::VFD* vfd) -> bool {
            // const uint8_t slave_addr = response[0]
            // const uint8_t function = response[1]
            // const uint8_t response_byte_count = response[2]

            uint16_t freq = (uint16_t(response[3]) << 8) | uint16_t(response[4]);
            vfd->_sync_dev_speed = freq;
            return true;
        };
    }

    VFD::response_parser DanfossVLT2800::get_status_ok(ModbusCommand& data) {
        data.tx_length = 6;  // including automatically set client_id, excluding crc
        data.rx_length = 5;  // excluding crc

        // Read out current state
        data.msg[1] = READ_COIL;
        data.msg[2] = 0x00;
        data.msg[3] = 0x20;  // Coil index 32
        data.msg[4] = 0x00;
        data.msg[5] = 0x10;  // Read 16 Bits

        return [](const uint8_t* response, Spindles::VFD* vfd) -> bool {
            // const uint8_t slave_addr = response[0]
            // const uint8_t function = response[1]
            // const uint8_t response_byte_count = response[2]
            // const uint8_t data_1 = response[3];  // Data (Coils 40-33)
            // const uint8_t data_2 = response[4];  // Data (Coils 48-41)

            const uint16_t statusword = int16_t(response[3]) | uint16_t(response[4] << 8);
            SpindleStatus  status;

            status.control_ready = statusword >> 0 & 1;
            status.drive_ready   = statusword >> 1 & 1;
            status.coasting_stop = statusword >> 2 & 1;
            status.trip          = statusword >> 3 & 1;

            status.trip_lock      = statusword >> 6 & 1;
            status.warning        = statusword >> 7 & 1;
            status.speed_status   = statusword >> 8 & 1;
            status.local_control  = statusword >> 9 & 1;
            status.freq_range_err = statusword >> 10 & 1;
            status.motor_running  = statusword >> 11 & 1;

            status.voltage_warn  = statusword >> 13 & 1;
            status.current_limit = statusword >> 14 & 1;
            status.thermal_warn  = statusword >> 15 & 1;

#ifdef DEBUG_VFD
            // Source: https://files.danfoss.com/download/Drives/MG10S202.pdf [Page 22]
            log_debug("Control ready:" << status.control_ready);
            log_debug("Drive ready:" << status.drive_ready);
            log_debug("Coasting stop:" << status.warning);
            log_debug("Trip status:" << status.trip);

            log_debug("Trip lock:" << status.trip_lock);
            log_debug("No warning/warning:" << status.warning);

            log_debug("Speed == ref:" << status.speed_status);
            log_debug("Local operation/serial communication control:" << status.local_control);
            log_debug("Outside frequency range:" << status.freq_range_err);
            log_debug("Motor running:" << status.motor_running);

            log_debug("Not used:" << status.voltage_warn);
            log_debug("Voltage warn:" << status.voltage_warn);
            log_debug("Current limit:" << status.current_limit);
            log_debug("Thermal warn:" << status.thermal_warn);
#endif

            log_error("TODO: actually check status bits and output potential errors");
            return true;
        };
    }

    namespace {
        SpindleFactory::InstanceBuilder<DanfossVLT2800> registration("DanfossVLT2800");
    }
}