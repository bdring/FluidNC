// Copyright (c) 2024 -  Jan Speckamp, whosmatt
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is for a Danfoss VLT 2800 VFD based spindle to be controlled via RS485 Modbus RTU.
    FluidNC imposes limitations (methods dont have access to full spindle state), while the Danfoss VFD expects the full state to be set with every command.
    As an interim solution, the state of the spindle is cached in cachedSpindleState. 

    Modbus setup of the VFD is covered in https://files.danfoss.com/download/Drives/doc_A_1_mg10s122.pdf
    General setup of the VFD is covered in https://files.danfoss.com/download/Drives/doc_B_1_MG28E902.pdf
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
        setupSpeeds(_maxFrequency);
    }

    void IRAM_ATTR DanfossVLT2800::set_speed_command(uint32_t dev_speed, ModbusCommand& data) {
        // Cache received speed
        cachedSpindleState.speed = dev_speed;

        // Write speed and direction from cache to VFD
        writeVFDState(cachedSpindleState, data);
    }

    void DanfossVLT2800::direction_command(SpindleState mode, ModbusCommand& data) {
        // Cache received direction
        cachedSpindleState.state = mode;

        // Write speed and direction from cache to VFD
        writeVFDState(cachedSpindleState, data);
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

    // The VLT2800 expects speed, direction and enable to be sent together at all times.
    // This function uses a combined cached spindle state that includes the speed and sends it to the VFD.
    void DanfossVLT2800::writeVFDState(combinedSpindleState spindle, ModbusCommand& data) {
        SpindleControl cword;
        cword.flags.coasting_stop    = 1;
        cword.flags.dc_braking_stop  = 1;
        cword.flags.quick_stop       = 1;
        cword.flags.freeze_freq      = 1;
        cword.flags.jog              = 0;
        cword.flags.reference_preset = 0;
        cword.flags.setup_preset     = 0;
        cword.flags.output_46        = 0;
        cword.flags.relay_01         = 0;
        cword.flags.data_valid       = 1;
        cword.flags.reset            = 0;
        cword.flags.reverse          = 0;

        switch (spindle.state) {
            case SpindleState::Cw:
                cword.flags.reverse    = 0;
                cword.flags.start_stop = 1;
                break;
            case SpindleState::Ccw:
                cword.flags.reverse    = 1;
                cword.flags.start_stop = 1;
                break;
            case SpindleState::Disable:
                cword.flags.start_stop = 0;
                break;
            default:
                break;
        }

        // Assemble packet:
        data.tx_length = 11;
        data.rx_length = 6;

        // We write a full control word instead of setting individual coils
        data.msg[1] = WRITE_MULTIPLE_COIL;
        data.msg[2] = 0x00;
        data.msg[3] = 0x00;  // start coil address
        data.msg[4] = 0x00;
        data.msg[5] = 0x20;  // write length
        data.msg[6] = 0x04;  // payload byte count

        data.msg[7] = cword.controlWord & 0xFF;  // MSB
        data.msg[8] = cword.controlWord >> 8;    // LSB

        data.msg[9]  = spindle.speed & 0xFF;
        data.msg[10] = spindle.speed >> 8;
    }
    namespace {
        SpindleFactory::InstanceBuilder<DanfossVLT2800> registration("DanfossVLT2800");
    }
}