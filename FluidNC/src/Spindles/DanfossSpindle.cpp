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
        data.msg[10] = dev_speed >> 8;
        data.msg[9]  = dev_speed & 0xFF;
    }

    void DanfossVLT2800::direction_command(SpindleState mode, ModbusCommand& data) {
#ifdef DEBUG_VFD
        log_debug("DanfossVLT2800::direction_command not implemented yet!");
#endif
        // fluidNC does not currently allow defining a VFDSpindle without direction command and data is always sent
        // send valid request to avoid error
        get_status_ok(data);
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

            uint16_t freq        = (uint16_t(response[3]) << 8) | uint16_t(response[4]);
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
            SpindleStatus status;
            status.statusWord = int16_t(response[3]) | uint16_t(response[4] << 8);  // See DanfossSpindle.h for structure

#ifdef DEBUG_VFD
            // Source: https://files.danfoss.com/download/Drives/MG10S202.pdf [Page 22]
            log_debug("Control ready:" << status.flags.control_ready);
            log_debug("Drive ready:" << status.flags.drive_ready);
            log_debug("Coasting stop:" << status.flags.warning);
            log_debug("Trip status:" << status.flags.trip);

            log_debug("Trip lock:" << status.flags.trip_lock);
            log_debug("No warning/warning:" << status.flags.warning);

            log_debug("Speed == ref:" << status.flags.speed_status);
            log_debug("Local operation/serial communication control:" << status.flags.local_control);
            log_debug("Outside frequency range:" << status.flags.freq_range_err);
            log_debug("Motor running:" << status.flags.motor_running);

            log_debug("Not used:" << status.flags.voltage_warn);
            log_debug("Voltage warn:" << status.flags.voltage_warn);
            log_debug("Current limit:" << status.flags.current_limit);
            log_debug("Thermal warn:" << status.flags.thermal_warn);
#endif

            log_error("TODO: actually check status bits and output potential errors");
            return true;
        };
    }

    namespace {
        SpindleFactory::InstanceBuilder<DanfossVLT2800> registration("DanfossVLT2800");
    }
}