// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    Dynamixel2.cpp

    This allows an Dynamixel servo to be used like any other motor. Servos
    do have limitation in travel and speed, so you do need to respect that.

    Protocol 2

    https://emanual.robotis.com/docs/en/dxl/protocol2/

*/

#include "Dynamixel2.h"

#include "../Machine/MachineConfig.h"
#include "../System.h"     // mpos_to_steps() etc
#include "../Limits.h"     // limitsMinPosition
#include "../NutsBolts.h"  // map(), mapConstrain()
#include "../Planner.h"    // plan_sync_position()

#include <cstdarg>
#include <cmath>

namespace MotorDrivers {
    Uart*   MotorDrivers::Dynamixel2::_uart     = nullptr;
    uint8_t MotorDrivers::Dynamixel2::_first_id = 0;
    uint8_t MotorDrivers::Dynamixel2::_last_id  = 0;

    uint8_t MotorDrivers::Dynamixel2::bulk_message[100];
    uint8_t MotorDrivers::Dynamixel2::bulk_message_index;

    uint8_t MotorDrivers::Dynamixel2::_dxl_rx_message[50] = {};  // received from dynamixel

    bool MotorDrivers::Dynamixel2::_uart_started = false;

    void Dynamixel2::init() {
        _has_errors = false;  // Initially assume okay
        _axis_index = axis_index();

        if (!_uart_started) {
            _uart->begin();
            if (_uart->setHalfDuplex()) {
                log_info("Dynamixel: UART set half duplex failed");
                return;
            }
            _uart->config_message("    dynamixel2", " ");
            _uart_started = true;
        }

        // for bulk updating
        if (_first_id == 0) {
            _first_id = _id;
        }
        _last_id = _id;

        config_message();  // print the config

        if (!test()) {  // ping the motor
            _has_errors = true;
            return;
        }

        set_disable(true);                              // turn off torque so we can set EEPROM registers
        set_operating_mode(DXL_CONTROL_MODE_POSITION);  // set it in the right control mode

        // servos will blink in axis order for reference
        LED_on(true);
        vTaskDelay(100);
        LED_on(false);

        startUpdateTask(_timer_ms);
    }

    void Dynamixel2::config_message() { log_info("    " << name() << " id::" << _id << " Count(" << _countMin << "," << _countMax << ")"); }

    bool Dynamixel2::test() {
        uint16_t len = 3;

        _dxl_tx_message[DXL_MSG_INSTR] = DXL_INSTR_PING;

        dxl_finish_message(_id, _dxl_tx_message, len);
        len = dxl_get_response(PING_RSP_LEN);  // wait for and get response

        if (len == PING_RSP_LEN) {
            uint16_t model_num = _dxl_rx_message[10] << 8 | _dxl_rx_message[9];
            if (model_num == 1060) {
                log_info("    Model XL430-W250 F/W Rev " << String(_dxl_rx_message[11], HEX));
            } else {
                log_info("    M/N " << model_num << " F/W Rev " << String(_dxl_rx_message[11], HEX));
            }
        } else {
            log_warn("    Ping failed");
            return false;
        }

        return true;
    }

    void Dynamixel2::read_settings() {}

    // sets the PWM to zero. This allows most servos to be manually moved
    void IRAM_ATTR Dynamixel2::set_disable(bool disable) {
        uint8_t param_count = 1;

        if (_disabled == disable) {
            return;
        }

        _disabled = disable;

        dxl_write(DXL_ADDR_TORQUE_EN, param_count, !_disabled);
    }

    void Dynamixel2::set_operating_mode(uint8_t mode) {
        uint8_t param_count = 1;
        dxl_write(DXL_OPERATING_MODE, param_count, mode);
    }

    void Dynamixel2::update() {
        if (_has_errors) {
            return;
        }

        if (_disabled) {
            // TO DO need to properly time this. Currently it would read too fast.
            // Needs delay between reads or bulk read.
            //dxl_read_position();
        } else {
            if (_id == _last_id) {  // from a LIFO List
                // initialize message
                bulk_message_index = DXL_MSG_INSTR;

                bulk_message[bulk_message_index]   = DXL_SYNC_WRITE;
                bulk_message[++bulk_message_index] = DXL_GOAL_POSITION & 0xFF;           // low order address
                bulk_message[++bulk_message_index] = (DXL_GOAL_POSITION & 0xFF00) >> 8;  // high order address
                bulk_message[++bulk_message_index] = 4;                                  // low order data length
                bulk_message[++bulk_message_index] = 0;                                  // high order data length
            }
            add_to_bulk_message();
            if (_id == _first_id) {
                send_bulk_message();
            }
        }
    }

    void Dynamixel2::set_location() {}

    // This motor will not do a standard home to a limit switch (maybe future)
    // If it is in the homing mask it will a quick move to $<axis>/Home/Mpos
    bool Dynamixel2::set_homing_mode(bool isHoming) {
        if (_has_errors) {
            return false;
        }

        auto axis                = config->_axes->_axis[_axis_index];
        motor_steps[_axis_index] = mpos_to_steps(axis->_homing->_mpos, _axis_index);

        set_disable(false);
        set_location();  // force the PWM to update now
        return false;    // Cannot do conventional homing
    }

    void Dynamixel2::dxl_goal_position(int32_t position) {
        uint8_t param_count = 4;

        dxl_write(DXL_GOAL_POSITION,
                  param_count,
                  (position & 0xFF),
                  (position & 0xFF00) >> 8,
                  (position & 0xFF0000) >> 16,
                  (position & 0xFF000000) >> 24);
    }

    uint32_t Dynamixel2::dxl_read_position() {
        uint16_t data_len = 4;

        dxl_read(DXL_PRESENT_POSITION, data_len);

        data_len = dxl_get_response(15);

        if (data_len == 15) {
            uint32_t dxl_position = _dxl_rx_message[9] | (_dxl_rx_message[10] << 8) | (_dxl_rx_message[11] << 16) |
                                    (_dxl_rx_message[12] << 24);

            auto axis = config->_axes->_axis[_axis_index];

            uint32_t pos_min_steps = lround(mpos_to_steps(limitsMinPosition(_axis_index), _axis_index));
            uint32_t pos_max_steps = lround(mpos_to_steps(limitsMaxPosition(_axis_index), _axis_index));

            uint32_t temp = myMap(dxl_position, _countMin, _countMax, pos_min_steps, pos_max_steps);

            motor_steps[_axis_index] = temp;

            plan_sync_position();

            return dxl_position;
        } else {
            return 0;
        }
    }

    void Dynamixel2::dxl_read(uint16_t address, uint16_t data_len) {
        uint8_t msg_len = 3 + 4;

        _dxl_tx_message[DXL_MSG_INSTR]     = DXL_READ;
        _dxl_tx_message[DXL_MSG_START]     = (address & 0xFF);            // low-order address value
        _dxl_tx_message[DXL_MSG_START + 1] = ((address & 0xFF00) >> 8);   // High-order address value
        _dxl_tx_message[DXL_MSG_START + 2] = (data_len & 0xFF);           // low-order data length value
        _dxl_tx_message[DXL_MSG_START + 3] = ((data_len & 0xFF00) >> 8);  // high-order address value

        dxl_finish_message(_id, _dxl_tx_message, msg_len);
    }

    void Dynamixel2::LED_on(bool on) {
        uint8_t param_count = 1;

        if (on)
            dxl_write(DXL_ADDR_LED_ON, param_count, 1);
        else
            dxl_write(DXL_ADDR_LED_ON, param_count, 0);
    }

    // wait for and get the servo response
    uint16_t Dynamixel2::dxl_get_response(uint16_t length) {
        length = _uart->readBytes(_dxl_rx_message, length, DXL_RESPONSE_WAIT_TICKS);
        return length;
    }

    void Dynamixel2::dxl_write(uint16_t address, uint8_t paramCount, ...) {
        _dxl_tx_message[DXL_MSG_INSTR]     = DXL_WRITE;
        _dxl_tx_message[DXL_MSG_START]     = (address & 0xFF);           // low-order address value
        _dxl_tx_message[DXL_MSG_START + 1] = ((address & 0xFF00) >> 8);  // High-order address value

        uint8_t msg_offset = 1;  // this is the offset from DXL_MSG_START in the message

        va_list valist;

        /* Initializing arguments  */
        va_start(valist, paramCount);

        for (int x = 0; x < paramCount; x++) {
            msg_offset++;
            _dxl_tx_message[DXL_MSG_START + msg_offset] = (uint8_t)va_arg(valist, int);
        }
        va_end(valist);  // Cleans up the list

        dxl_finish_message(_id, _dxl_tx_message, msg_offset + 4);

        uint16_t len = 11;  // response length
        len          = dxl_get_response(len);

        if (len == 11) {
            uint8_t err = _dxl_rx_message[8];
            switch (err) {
                case 1:
                    log_info(name() << " ID " << _id << " Write fail error");
                    break;
                case 2:
                    log_info(name() << " ID " << _id << " Write instruction error");
                    break;
                case 3:
                    log_info(name() << " ID " << _id << " Write access error");
                    break;
                case 4:
                    log_info(name() << " ID " << _id << " Write data range error");
                    break;
                case 5:
                    log_info(name() << " ID " << _id << " Write data length error");
                    break;
                case 6:
                    log_info(name() << " ID " << _id << " Write data limit error");
                    break;
                case 7:
                    log_info(name() << " ID " << _id << " Write access error");
                    break;
                default:
                    break;
            }
        } else {
            // timeout
            log_info(name() << " ID " << _id << " Timeout");
        }
    }

    void Dynamixel2::add_to_bulk_message() {
        float    dxl_count_min, dxl_count_max;
        uint32_t dxl_position;

        float* mpos = get_mpos();

        dxl_count_min = float(_countMin);
        dxl_count_max = float(_countMax);

        // map the mm range to the servo range
        dxl_position = static_cast<uint32_t>(
            mapConstrain(mpos[_axis_index], limitsMinPosition(_axis_index), limitsMaxPosition(_axis_index), dxl_count_min, dxl_count_max));

        log_debug("dxl:" << _id << " pos:" << dxl_position);

        bulk_message[++bulk_message_index] = _id;                                // ID of the servo
        bulk_message[++bulk_message_index] = dxl_position & 0xFF;                // data
        bulk_message[++bulk_message_index] = (dxl_position & 0xFF00) >> 8;       // data
        bulk_message[++bulk_message_index] = (dxl_position & 0xFF0000) >> 16;    // data
        bulk_message[++bulk_message_index] = (dxl_position & 0xFF000000) >> 24;  // data
    }

    void Dynamixel2::send_bulk_message() { dxl_finish_message(DXL_BROADCAST_ID, bulk_message, bulk_message_index - DXL_MSG_INSTR + 1); }

    /*
    Static

    This is a helper function to complete and send the message
    The body of the message should be in msg, at the correct location
    before calling this function.
    This function will add the header, length bytes and CRC
    It will then send the message
*/
    void Dynamixel2::dxl_finish_message(uint8_t id, uint8_t* msg, uint16_t msg_len) {
        uint16_t crc = 0;
        // header
        msg[DXL_MSG_HDR1] = char(0xFF);
        msg[DXL_MSG_HDR2] = char(0xFF);
        msg[DXL_MSG_HDR3] = char(0xFD);
        //
        // reserved
        msg[DXL_MSG_RSRV] = 0x00;
        msg[DXL_MSG_ID]   = id;
        // length
        msg[DXL_MSG_LEN_L] = msg_len & 0xFF;
        msg[DXL_MSG_LEN_H] = (msg_len & 0xFF00) >> 8;

        // the message should already be here

        crc = dxl_update_crc(crc, msg, 5 + msg_len);

        msg[msg_len + 5] = crc & 0xFF;  // CRC_L
        msg[msg_len + 6] = (crc & 0xFF00) >> 8;

        _uart->flush();
        _uart->write(msg, msg_len + 7);
    }

    // from http://emanual.robotis.com/docs/en/dxl/crc/
    uint16_t Dynamixel2::dxl_update_crc(uint16_t crc_accum, uint8_t* data_blk_ptr, uint8_t data_blk_size) {
        uint16_t i, j;
        uint16_t crc_table[256] = {
            0x0000, 0x8005, 0x800F, 0x000A, 0x801B, 0x001E, 0x0014, 0x8011, 0x8033, 0x0036, 0x003C, 0x8039, 0x0028, 0x802D, 0x8027, 0x0022,
            0x8063, 0x0066, 0x006C, 0x8069, 0x0078, 0x807D, 0x8077, 0x0072, 0x0050, 0x8055, 0x805F, 0x005A, 0x804B, 0x004E, 0x0044, 0x8041,
            0x80C3, 0x00C6, 0x00CC, 0x80C9, 0x00D8, 0x80DD, 0x80D7, 0x00D2, 0x00F0, 0x80F5, 0x80FF, 0x00FA, 0x80EB, 0x00EE, 0x00E4, 0x80E1,
            0x00A0, 0x80A5, 0x80AF, 0x00AA, 0x80BB, 0x00BE, 0x00B4, 0x80B1, 0x8093, 0x0096, 0x009C, 0x8099, 0x0088, 0x808D, 0x8087, 0x0082,
            0x8183, 0x0186, 0x018C, 0x8189, 0x0198, 0x819D, 0x8197, 0x0192, 0x01B0, 0x81B5, 0x81BF, 0x01BA, 0x81AB, 0x01AE, 0x01A4, 0x81A1,
            0x01E0, 0x81E5, 0x81EF, 0x01EA, 0x81FB, 0x01FE, 0x01F4, 0x81F1, 0x81D3, 0x01D6, 0x01DC, 0x81D9, 0x01C8, 0x81CD, 0x81C7, 0x01C2,
            0x0140, 0x8145, 0x814F, 0x014A, 0x815B, 0x015E, 0x0154, 0x8151, 0x8173, 0x0176, 0x017C, 0x8179, 0x0168, 0x816D, 0x8167, 0x0162,
            0x8123, 0x0126, 0x012C, 0x8129, 0x0138, 0x813D, 0x8137, 0x0132, 0x0110, 0x8115, 0x811F, 0x011A, 0x810B, 0x010E, 0x0104, 0x8101,
            0x8303, 0x0306, 0x030C, 0x8309, 0x0318, 0x831D, 0x8317, 0x0312, 0x0330, 0x8335, 0x833F, 0x033A, 0x832B, 0x032E, 0x0324, 0x8321,
            0x0360, 0x8365, 0x836F, 0x036A, 0x837B, 0x037E, 0x0374, 0x8371, 0x8353, 0x0356, 0x035C, 0x8359, 0x0348, 0x834D, 0x8347, 0x0342,
            0x03C0, 0x83C5, 0x83CF, 0x03CA, 0x83DB, 0x03DE, 0x03D4, 0x83D1, 0x83F3, 0x03F6, 0x03FC, 0x83F9, 0x03E8, 0x83ED, 0x83E7, 0x03E2,
            0x83A3, 0x03A6, 0x03AC, 0x83A9, 0x03B8, 0x83BD, 0x83B7, 0x03B2, 0x0390, 0x8395, 0x839F, 0x039A, 0x838B, 0x038E, 0x0384, 0x8381,
            0x0280, 0x8285, 0x828F, 0x028A, 0x829B, 0x029E, 0x0294, 0x8291, 0x82B3, 0x02B6, 0x02BC, 0x82B9, 0x02A8, 0x82AD, 0x82A7, 0x02A2,
            0x82E3, 0x02E6, 0x02EC, 0x82E9, 0x02F8, 0x82FD, 0x82F7, 0x02F2, 0x02D0, 0x82D5, 0x82DF, 0x02DA, 0x82CB, 0x02CE, 0x02C4, 0x82C1,
            0x8243, 0x0246, 0x024C, 0x8249, 0x0258, 0x825D, 0x8257, 0x0252, 0x0270, 0x8275, 0x827F, 0x027A, 0x826B, 0x026E, 0x0264, 0x8261,
            0x0220, 0x8225, 0x822F, 0x022A, 0x823B, 0x023E, 0x0234, 0x8231, 0x8213, 0x0216, 0x021C, 0x8219, 0x0208, 0x820D, 0x8207, 0x0202
        };

        for (j = 0; j < data_blk_size; j++) {
            i         = ((uint16_t)(crc_accum >> 8) ^ data_blk_ptr[j]) & 0xFF;
            crc_accum = (crc_accum << 8) ^ crc_table[i];
        }

        return crc_accum;
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<Dynamixel2> registration("dynamixel2");
    }
}
