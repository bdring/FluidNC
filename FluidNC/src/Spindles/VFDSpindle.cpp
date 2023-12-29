// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2020 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is for a VFD based spindles via RS485 Modbus. The details of the 
    VFD protocol heavily depend on the VFD in question here. We have some 
    implementations, but if yours is not here, the place to start is the 
    manual. This VFD class implements the modbus functionality.

                         WARNING!!!!
    VFDs are very dangerous. They have high voltages and are very powerful
    Remove power before changing bits.

    TODO:
      - We can report spindle_state and rpm better with VFD's that support 
        either mode, register RPM or actual RPM.
      - Destructor should break down the task.

*/
#include "VFDSpindle.h"

#include "src/Machine/MachineConfig.h"
#include "src/MotionControl.h"  // mc_critical
#include "src/Protocol.h"       // rtAlarm
#include "src/Report.h"         // hex message
#include "src/Configuration/HandlerType.h"

#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>

const int        VFD_RS485_BUF_SIZE   = 127;
const int        VFD_RS485_QUEUE_SIZE = 10;                                     // number of commands that can be queued up.
const int        RESPONSE_WAIT_MS     = 1000;                                   // how long to wait for a response
const int        VFD_RS485_POLL_RATE  = 250;                                    // in milliseconds between commands
const TickType_t response_ticks       = RESPONSE_WAIT_MS / portTICK_PERIOD_MS;  // in milliseconds between commands

namespace Spindles {
    QueueHandle_t VFD::vfd_cmd_queue     = nullptr;
    TaskHandle_t  VFD::vfd_cmdTaskHandle = nullptr;

    void VFD::reportParsingErrors(ModbusCommand cmd, uint8_t* rx_message, size_t read_length) {
#ifdef DEBUG_VFD
        hex_msg(cmd.msg, "RS485 Tx: ", cmd.tx_length);
        hex_msg(rx_message, "RS485 Rx: ", read_length);
#endif
    }
    void VFD::reportCmdErrors(ModbusCommand cmd, uint8_t* rx_message, size_t read_length, uint8_t id) {
#ifdef DEBUG_VFD
        hex_msg(cmd.msg, "RS485 Tx: ", cmd.tx_length);
        hex_msg(rx_message, "RS485 Rx: ", read_length);

        if (read_length != 0) {
            if (rx_message[0] != id) {
                log_info("RS485 received message from other modbus device");
            } else if (read_length != cmd.rx_length) {
                log_info("RS485 received message of unexpected length; expected:" << int(cmd.rx_length) << " got:" << int(read_length));
            } else {
                log_info("RS485 CRC check failed");
            }
        } else {
            log_info("RS485 No response");
        }
#endif
    }

    // The communications task
    void VFD::vfd_cmd_task(void* pvParameters) {
        static bool unresponsive = false;  // to pop off a message once each time it becomes unresponsive
        static int  pollidx      = -1;

        VFD*          instance = static_cast<VFD*>(pvParameters);
        auto&         uart     = *instance->_uart;
        ModbusCommand next_cmd;
        uint8_t       rx_message[VFD_RS485_MAX_MSG_SIZE];
        bool          safetyPollingEnabled = instance->safety_polling();

        for (; true; delay_ms(VFD_RS485_POLL_RATE)) {
            std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // read fence for settings
            response_parser parser = nullptr;

            // First check if we should ask the VFD for the speed parameters as part of the initialization.
            if (pollidx < 0 && (parser = instance->initialization_sequence(pollidx, next_cmd)) != nullptr) {
            } else {
                pollidx = 1;  // Done with initialization. Main sequence.
            }
            next_cmd.critical = false;

            VFDaction action;
            if (parser == nullptr) {
                // If we don't have a parser, the queue goes first.
                if (xQueueReceive(vfd_cmd_queue, &action, 0)) {
                    switch (action.action) {
                        case actionSetSpeed:
                            if (!instance->prepareSetSpeedCommand(action.arg, next_cmd)) {
                                // prepareSetSpeedCommand() can return false if the speed
                                // change is unnecessary - already at that speed.
                                // In that case we just discard the command.
                                continue;  // main loop
                            }
                            next_cmd.critical = action.critical;
                            break;
                        case actionSetMode:
                            log_debug("vfd_cmd_task mode:" << action.action);
                            if (!instance->prepareSetModeCommand(SpindleState(action.arg), next_cmd)) {
                                continue;  // main loop
                            }
                            next_cmd.critical = action.critical;
                            break;
                    }
                } else {
                    // We do not have a parser and there is nothing in the queue, so we cycle
                    // through the set of periodic queries.

                    // We poll in a cycle. Note that the switch will fall through unless we encounter a hit.
                    // The weakest form here is 'get_status_ok' which should be implemented if the rest fails.
                    if (instance->_syncing) {
                        parser = instance->get_current_speed(next_cmd);
                    } else if (safetyPollingEnabled) {
                        switch (pollidx) {
                            case 1:
                                parser = instance->get_current_speed(next_cmd);
                                if (parser) {
                                    pollidx = 2;
                                    break;
                                }
                                // fall through if get_current_speed did not return a parser
                            case 2:
                                parser = instance->get_current_direction(next_cmd);
                                if (parser) {
                                    pollidx = 3;
                                    break;
                                }
                                // fall through if get_current_direction did not return a parser
                            case 3:
                            default:
                                parser  = instance->get_status_ok(next_cmd);
                                pollidx = 1;

                                // we could complete this in case parser == nullptr with some ifs, but let's
                                // just keep it easy and wait an iteration.
                                break;
                        }
                    }

                    // If we have no parser, that means get_status_ok is not implemented (and we have
                    // nothing resting in our queue). Let's fall back on a simple continue.
                    if (parser == nullptr) {
                        continue;  // main loop
                    }
                }
            }

            // At this point next_cmd has been filled with a command block
            {
                // Fill in the fields that are the same for all protocol variants
                next_cmd.msg[0] = instance->_modbus_id;

                // Grabbed the command. Add the CRC16 checksum:
                auto crc16                         = ModRTU_CRC(next_cmd.msg, next_cmd.tx_length);
                next_cmd.msg[next_cmd.tx_length++] = (crc16 & 0xFF);
                next_cmd.msg[next_cmd.tx_length++] = (crc16 & 0xFF00) >> 8;
                next_cmd.rx_length += 2;

#ifdef DEBUG_VFD_ALL
                if (parser == nullptr) {
                    hex_msg(next_cmd.msg, "RS485 Tx: ", next_cmd.tx_length);
                }
#endif
            }

            // Assume for the worst, and retry...
            int retry_count = 0;
            for (; retry_count < MAX_RETRIES; ++retry_count) {
                // Flush the UART and write the data:
                uart.flush();
                uart.write(next_cmd.msg, next_cmd.tx_length);
                uart.flushTxTimed(response_ticks);

                // Read the response
                size_t read_length  = 0;
                size_t current_read = uart.timedReadBytes(rx_message, next_cmd.rx_length, response_ticks);
                read_length += current_read;

                // Apparently some Huanyang report modbus errors in the correct way, and the rest not. Sigh.
                // Let's just check for the condition, and truncate the first byte.
                if (read_length > 0 && instance->_modbus_id != 0 && rx_message[0] == 0) {
                    memmove(rx_message + 1, rx_message, read_length - 1);
                }

                while (read_length < next_cmd.rx_length && current_read > 0) {
                    // Try to read more; we're not there yet...
                    current_read = uart.timedReadBytes(rx_message + read_length, next_cmd.rx_length - read_length, response_ticks);
                    read_length += current_read;
                }

                // Generate crc16 for the response:
                auto crc16response = ModRTU_CRC(rx_message, next_cmd.rx_length - 2);

                if (read_length == next_cmd.rx_length &&                             // check expected length
                    rx_message[0] == instance->_modbus_id &&                         // check address
                    rx_message[read_length - 1] == (crc16response & 0xFF00) >> 8 &&  // check CRC byte 1
                    rx_message[read_length - 2] == (crc16response & 0xFF)) {         // check CRC byte 1

                    // Success
                    unresponsive = false;
                    retry_count  = MAX_RETRIES + 1;  // stop retry'ing

                    // Should we parse this?
                    if (parser != nullptr) {
                        if (parser(rx_message, instance)) {
                            // If we're initializing, move to the next initialization command:
                            if (pollidx < 0) {
                                --pollidx;
                            }
                        } else {
                            // Parsing failed
                            reportParsingErrors(next_cmd, rx_message, read_length);

                            // If we were initializing, move back to where we started.
                            unresponsive = true;
                            pollidx      = -1;  // Re-initializing the VFD seems like a plan
                            log_info("Spindle RS485 did not give a satisfying response");
                        }
                    }
                } else {
                    reportCmdErrors(next_cmd, rx_message, read_length, instance->_modbus_id);

                    // Wait a bit before we retry. Set the delay to poll-rate. Not sure
                    // if we should use a different value...
                    delay_ms(VFD_RS485_POLL_RATE);

#ifdef DEBUG_TASK_STACK
                    static UBaseType_t uxHighWaterMark = 0;
                    reportTaskStackSize(uxHighWaterMark);
#endif
                }
            }

            if (retry_count == MAX_RETRIES) {
                if (!unresponsive) {
                    log_info("VFD RS485 Unresponsive");
                    unresponsive = true;
                    pollidx      = -1;
                }
                if (next_cmd.critical) {
                    mc_critical(ExecAlarm::SpindleControl);
                    log_error("Critical VFD RS485 Unresponsive");
                }
            }
        }
    }

    // ================== Class methods ==================================

    void VFD::init() {
        _sync_dev_speed = 0;
        _syncing        = false;

        // The following lets you have either a uart: section below the VFD section,
        // or "uart_num: N" referring to an externally defined uartN: section - but not both
        if (_uart) {
            _uart->begin();
        } else {
            _uart = config->_uarts[_uart_num];
            if (!_uart) {
                log_error("VFDSpindle: Missing uart" << _uart_num << "section");
                return;
            }
        }

        if (_uart->setHalfDuplex()) {
            log_info("VFD: RS485 UART set half duplex failed");
            return;
        }

        // These VFDs are always reversable, but most can be set via the operator panel
        // to only allow one direction.  In principle we could check that setting and
        // automatically set is_reversable.
        is_reversable = true;

        _current_state = SpindleState::Disable;

        // Initialization is complete, so now it's okay to run the queue task:
        if (!vfd_cmd_queue) {  // init can happen many times, we only want to start one task
            vfd_cmd_queue = xQueueCreate(VFD_RS485_QUEUE_SIZE, sizeof(VFDaction));
            xTaskCreatePinnedToCore(vfd_cmd_task,         // task
                                    "vfd_cmdTaskHandle",  // name for task
                                    2048,                 // size of task stack
                                    this,                 // parameters
                                    1,                    // priority
                                    &vfd_cmdTaskHandle,
                                    SUPPORT_TASK_CORE  // core
            );
        }

        config_message();

        set_mode(SpindleState::Disable, true);
    }

    void VFD::config_message() { _uart->config_message(name(), " Spindle "); }

    void VFD::setState(SpindleState state, SpindleSpeed speed) {
        log_debug("VFD setState:" << uint8_t(state) << " SpindleSpeed:" << speed);
        if (sys.abort) {
            return;  // Block during abort.
        }

        bool critical = (sys.state == State::Cycle || state != SpindleState::Disable);

        uint32_t dev_speed = mapSpeed(speed);
        log_debug("RPM:" << speed << " mapped to device units:" << dev_speed);

        if (_current_state != state) {
            // Changing state
            set_mode(state, critical);  // critical if we are in a job

            setSpeed(dev_speed);
        } else {
            // Not changing state
            if (_current_dev_speed != dev_speed) {
                // Changing speed
                setSpeed(dev_speed);
            }
        }
        if (use_delay_settings()) {
            spindleDelay(state, speed);
        } else {
            // _sync_dev_speed is set by a callback that handles
            // responses from periodic get_current_speed() requests.
            // It changes as the actual speed ramps toward the target.

            _syncing = true;  // poll for speed

            auto minSpeedAllowed = dev_speed > _slop ? (dev_speed - _slop) : 0;
            auto maxSpeedAllowed = dev_speed + _slop;

            int       unchanged = 0;
            const int limit     = 20;  // 20 * 0.5s = 10 sec
            auto      last      = _sync_dev_speed;

            while ((_last_override_value == sys.spindle_speed_ovr) &&  // skip if the override changes
                   ((_sync_dev_speed < minSpeedAllowed || _sync_dev_speed > maxSpeedAllowed) && unchanged < limit)) {
#ifdef DEBUG_VFD
                log_debug("Syncing speed. Requested: " << int(dev_speed) << " current:" << int(_sync_dev_speed));
#endif
                // if (!mc_dwell(500)) {
                //     // Something happened while we were dwelling, like a safety door.
                //     unchanged = limit;
                //     last      = _sync_dev_speed;
                //     break;
                // }
                delay_ms(500);

                // unchanged counts the number of consecutive times that we see the same speed
                unchanged = (_sync_dev_speed == last) ? unchanged + 1 : 0;
                last      = _sync_dev_speed;
            }
            _last_override_value = sys.spindle_speed_ovr;

#ifdef DEBUG_VFD
            log_debug("Synced speed. Requested:" << int(dev_speed) << " current:" << int(_sync_dev_speed));
#endif

            if (unchanged == limit) {
                mc_critical(ExecAlarm::SpindleControl);
                log_error(name() << " spindle did not reach device units " << dev_speed << ". Reported value is " << _sync_dev_speed);
            }

            _syncing = false;
            // spindleDelay() sets these when it is used
            _current_state = state;
            _current_speed = speed;
        }
        //        }
    }

    bool VFD::prepareSetModeCommand(SpindleState mode, ModbusCommand& data) {
        // Do variant-specific command preparation
        direction_command(mode, data);

        if (mode == SpindleState::Disable) {
            if (!xQueueReset(vfd_cmd_queue)) {
                log_info(name() << " spindle off, queue could not be reset");
            }
        }

        _current_state = mode;
        return true;
    }

    void VFD::set_mode(SpindleState mode, bool critical) {
        _last_override_value = sys.spindle_speed_ovr;  // sync these on mode changes
        if (vfd_cmd_queue) {
            VFDaction action;
            action.action   = actionSetMode;
            action.arg      = uint32_t(mode);
            action.critical = critical;
            if (xQueueSend(vfd_cmd_queue, &action, 0) != pdTRUE) {
                log_info("VFD Queue Full");
            }
        }
    }

    void IRAM_ATTR VFD::setSpeedfromISR(uint32_t dev_speed) {
        if (_current_dev_speed == dev_speed || _last_speed == dev_speed) {
            return;
        }

        _last_speed = dev_speed;

        if (vfd_cmd_queue) {
            VFDaction action;
            action.action   = actionSetSpeed;
            action.arg      = dev_speed;
            action.critical = (dev_speed == 0);
            // Ignore errors because reporting is not safe from an ISR.
            // Perhaps set a flag instead?
            xQueueSendFromISR(vfd_cmd_queue, &action, 0);
        }
    }

    void VFD::setSpeed(uint32_t dev_speed) {
        if (vfd_cmd_queue) {
            VFDaction action;
            action.action   = actionSetSpeed;
            action.arg      = dev_speed;
            action.critical = dev_speed == 0;
            if (xQueueSend(vfd_cmd_queue, &action, 0) != pdTRUE) {
                log_info("VFD Queue Full");
            }
        }
    }

    bool VFD::prepareSetSpeedCommand(uint32_t speed, ModbusCommand& data) {
        log_debug("prep speed " << speed << " curr " << _current_dev_speed);
        if (speed == _current_dev_speed) {  // prevent setting same speed twice
            return false;
        }
        _current_dev_speed = speed;

#ifdef DEBUG_VFD_ALL
        log_debug("Setting spindle speed to:" << int(speed));
#endif
        // Do variant-specific command preparation
        set_speed_command(speed, data);

        // Sometimes sync_dev_speed is retained between different set_speed_command's. We don't want that - we want
        // spindle sync to kick in after we set the speed. This forces that.
        _sync_dev_speed = UINT32_MAX;

        return true;
    }

    // Calculate the CRC on all of the byte except the last 2
    // It then added the CRC to those last 2 bytes
    // full_msg_len This is the length of the message including the 2 crc bytes
    // Source: https://ctlsys.com/support/how_to_compute_the_modbus_rtu_message_crc/
    uint16_t VFD::ModRTU_CRC(uint8_t* buf, int msg_len) {
        uint16_t crc = 0xFFFF;
        for (int pos = 0; pos < msg_len; pos++) {
            crc ^= uint16_t(buf[pos]);  // XOR byte into least sig. byte of crc.

            for (int i = 8; i != 0; i--) {  // Loop over each bit
                if ((crc & 0x0001) != 0) {  // If the LSB is set
                    crc >>= 1;              // Shift right and XOR 0xA001
                    crc ^= 0xA001;
                } else {        // Else LSB is not set
                    crc >>= 1;  // Just shift right
                }
            }
        }

        return crc;
    }
    void VFD::validate() {
        Spindle::validate();
        Assert(_uart != nullptr || _uart_num != -1, "VFD: missing UART configuration");
    }

    void VFD::group(Configuration::HandlerBase& handler) {
        if (handler.handlerType() == Configuration::HandlerType::Generator) {
            if (_uart_num == -1) {
                handler.section("uart", _uart, 1);
            } else {
                handler.item("uart_num", _uart_num);
            }
        } else {
            handler.section("uart", _uart, 1);
            handler.item("uart_num", _uart_num);
        }
        handler.item("modbus_id", _modbus_id, 0, 247);  // per https://modbus.org/docs/PI_MBUS_300.pdf

        Spindle::group(handler);
    }
}
