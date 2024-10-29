#include "VFDProtocol.h"

#include "../VFDSpindle.h"
#include "../../MotionControl.h"  // mc_critical

#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>

namespace Spindles 
{
    namespace VFD 
    {
        const int        VFD_RS485_BUF_SIZE   = 127;
        const int        RESPONSE_WAIT_MS     = 1000;                                   // how long to wait for a response
        const int        VFD_RS485_POLL_RATE  = 250;                                    // in milliseconds between commands
        const TickType_t response_ticks       = RESPONSE_WAIT_MS / portTICK_PERIOD_MS;  // in milliseconds between commands

        QueueHandle_t VFDProtocol::vfd_cmd_queue     = nullptr;
        TaskHandle_t  VFDProtocol::vfd_cmdTaskHandle = nullptr;

        void VFDProtocol::reportParsingErrors(ModbusCommand cmd, uint8_t* rx_message, size_t read_length) {
    #ifdef DEBUG_VFD
            hex_msg(cmd.msg, "RS485 Tx: ", cmd.tx_length);
            hex_msg(rx_message, "RS485 Rx: ", read_length);
    #endif
        }
        void VFDProtocol::reportCmdErrors(ModbusCommand cmd, uint8_t* rx_message, size_t read_length, uint8_t id) {
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
        void VFDProtocol::vfd_cmd_task(void* pvParameters) {
            static bool unresponsive = false;  // to pop off a message once each time it becomes unresponsive
            static int  pollidx      = -1;

            VFDSpindle*   instance = static_cast<VFDSpindle*>(pvParameters);
            auto          impl     = instance->detail_;
            auto&         uart     = *instance->_uart;
            ModbusCommand next_cmd;
            uint8_t       rx_message[VFD_RS485_MAX_MSG_SIZE];
            bool          safetyPollingEnabled = impl->safety_polling();

            for (; true; delay_ms(VFD_RS485_POLL_RATE)) {
                std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // read fence for settings
                response_parser parser = nullptr;

                // First check if we should ask the VFD for the speed parameters as part of the initialization.
                if (pollidx < 0 && (parser = impl->initialization_sequence(pollidx, next_cmd)) != nullptr) {
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
                                if (!impl->prepareSetSpeedCommand(action.arg, next_cmd, instance)) {
                                    // prepareSetSpeedCommand() can return false if the speed
                                    // change is unnecessary - already at that speed.
                                    // In that case we just discard the command.
                                    continue;  // main loop
                                }
                                next_cmd.critical = action.critical;
                                break;
                            case actionSetMode:
                                log_debug("vfd_cmd_task mode:" << action.action);
                                if (!impl->prepareSetModeCommand(SpindleState(action.arg), next_cmd, instance)) {
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
                            parser = impl->get_current_speed(next_cmd);
                        } else if (safetyPollingEnabled) {
                            switch (pollidx) {
                                case 1:
                                    parser = impl->get_current_speed(next_cmd);
                                    if (parser) {
                                        pollidx = 2;
                                        break;
                                    }
                                    // fall through if get_current_speed did not return a parser
                                case 2:
                                    parser = impl->get_current_direction(next_cmd);
                                    if (parser) {
                                        pollidx = 3;
                                        break;
                                    }
                                    // fall through if get_current_direction did not return a parser
                                case 3:
                                default:
                                    parser  = impl->get_status_ok(next_cmd);
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
                            if (parser(rx_message, instance, impl)) {
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

        bool VFDProtocol::prepareSetModeCommand(SpindleState mode, ModbusCommand& data, VFDSpindle* spindle) {
            // Do variant-specific command preparation
            direction_command(mode, data);

            if (mode == SpindleState::Disable) {
                if (!xQueueReset(vfd_cmd_queue)) {
                    log_info(spindle->name() << " spindle off, queue could not be reset");
                }
            }

            spindle->_current_state = mode;
            return true;
        }

        bool VFDProtocol::prepareSetSpeedCommand(uint32_t speed, ModbusCommand& data, VFDSpindle* spindle) {
            log_debug("prep speed " << speed << " curr " << spindle->_current_dev_speed);
            if (speed == spindle->_current_dev_speed) {  // prevent setting same speed twice
                return false;
            }
            spindle->_current_dev_speed = speed;

    #ifdef DEBUG_VFD_ALL
            log_debug("Setting spindle speed to:" << int(speed));
    #endif
            // Do variant-specific command preparation
            set_speed_command(speed, data);

            // Sometimes sync_dev_speed is retained between different set_speed_command's. We don't want that - we want
            // spindle sync to kick in after we set the speed. This forces that.
            spindle->_sync_dev_speed = UINT32_MAX;

            return true;
        }

        // Calculate the CRC on all of the byte except the last 2
        // It then added the CRC to those last 2 bytes
        // full_msg_len This is the length of the message including the 2 crc bytes
        // Source: https://ctlsys.com/support/how_to_compute_the_modbus_rtu_message_crc/
        uint16_t VFDProtocol::ModRTU_CRC(uint8_t* buf, int msg_len) {
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
    }
}
