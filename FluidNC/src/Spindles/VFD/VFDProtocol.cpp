#include "VFDProtocol.h"

#include "Spindles/VFDSpindle.h"
#include "MotionControl.h"  // mc_critical

#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>

namespace Spindles {
    namespace VFD {
        const int        VFD_RS485_BUF_SIZE = 127;
        const int        RESPONSE_WAIT_MS   = 100;                                    // how long to wait for a response
        const TickType_t response_ticks     = RESPONSE_WAIT_MS / portTICK_PERIOD_MS;  // in milliseconds between commands

        QueueHandle_t VFDProtocol::vfd_cmd_queue     = nullptr;
        QueueHandle_t VFDProtocol::vfd_speed_queue   = nullptr;
        TaskHandle_t  VFDProtocol::vfd_cmdTaskHandle = nullptr;

        void VFDProtocol::reportParsingErrors(ModbusCommand cmd, uint8_t* rx_message, size_t read_length) {}
        bool VFDProtocol::checkRx(ModbusCommand cmd, uint8_t* rx_message, size_t read_length, uint8_t id) {
            if (read_length == 0) {
                log_info("RS485 No response");
                return false;
            }
            if (rx_message[0] != id) {
                log_info("RS485 received message from other modbus device");
                return false;
            }
            if (read_length != cmd.rx_length) {
                log_info("RS485 received message of unexpected length; expected:" << int(cmd.rx_length) << " got:" << int(read_length));
                return false;
            }

            auto crc16response = ModRTU_CRC(rx_message, cmd.rx_length - 2);
            if (rx_message[read_length - 1] != (crc16response & 0xFF00) >> 8 || rx_message[read_length - 2] != (crc16response & 0xFF)) {
                log_info("RS485 CRC check failed");
                return false;
            }
            return true;
        }

        // The communications task
        void VFDProtocol::vfd_cmd_task(void* pvParameters) {
            static bool    unresponsive = false;  // to pop off a message once each time it becomes unresponsive
            static int32_t pollidx      = -1;

            VFDSpindle*   instance = static_cast<VFDSpindle*>(pvParameters);
            auto          impl     = instance->detail_;
            auto&         uart     = *instance->_uart;
            ModbusCommand cmd;
            uint8_t       rx_message[VFD_RS485_MAX_MSG_SIZE];
            bool          safetyPollingEnabled = impl->safety_polling();

            for (; true; delay_ms(instance->_poll_ms)) {
                std::atomic_thread_fence(std::memory_order_seq_cst);  // read fence for settings
                response_parser parser = nullptr;

                // First check if we should ask the VFD for the speed parameters as part of the initialization.
                if (pollidx < 0) {
                    if ((parser = impl->initialization_sequence(pollidx, cmd, instance)) == nullptr) {
                        pollidx = 1;  // Done with initialization. Main sequence.
                    }
                }
                cmd.critical = false;

                VFDaction action;
                if (parser == nullptr) {
                    // If we don't have a parser, the queue goes first.
                    if (xQueueReceive(vfd_cmd_queue, &action, 0)) {
                        switch (action.action) {
                            case actionSetSpeed:
                                if (!impl->prepareSetSpeedCommand(action.arg, cmd, instance)) {
                                    // prepareSetSpeedCommand() can return false if the speed
                                    // change is unnecessary - already at that speed.
                                    // In that case we just discard the command.
                                    continue;  // main loop
                                }
                                cmd.critical = action.critical;
                                break;
                            case actionSetMode:
                                if (!impl->prepareSetModeCommand(SpindleState(action.arg), cmd, instance)) {
                                    continue;  // main loop
                                }
                                cmd.critical = action.critical;
                                break;
                        }
                    } else {
                        // We do not have a parser and there is nothing in the queue, so we cycle
                        // through the set of periodic queries.

                        // We poll in a cycle. Note that the switch will fall through unless we encounter a hit.
                        // The weakest form here is 'get_status_ok' which should be implemented if the rest fails.
                        if (instance->_syncing) {
                            parser = impl->get_current_speed(cmd);
                        } else if (safetyPollingEnabled) {
                            switch (pollidx) {
                                case 1:
                                    parser = impl->get_current_speed(cmd);
                                    if (parser) {
                                        pollidx = 2;
                                        break;
                                    }
                                    // fall through if get_current_speed did not return a parser
                                    [[fallthrough]];
                                case 2:
                                    parser = impl->get_current_direction(cmd);
                                    if (parser) {
                                        pollidx = 3;
                                        break;
                                    }
                                    // fall through if get_current_direction did not return a parser
                                    [[fallthrough]];
                                case 3:
                                default:
                                    parser  = impl->get_status_ok(cmd);
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

                // At this point cmd has been filled with a command block
                // Fill in the fields that are the same for all protocol variants
                cmd.msg[0] = instance->_modbus_id;

                // Grabbed the command. Add the CRC16 checksum:
                auto crc16               = ModRTU_CRC(cmd.msg, cmd.tx_length);
                cmd.msg[cmd.tx_length++] = (crc16 & 0xFF);
                cmd.msg[cmd.tx_length++] = (crc16 & 0xFF00) >> 8;
                cmd.rx_length += 2;

                // Assume for the worst, and retry...
                size_t retry_count = 0;
                for (; retry_count < instance->_retries; ++retry_count) {
                    // Flush the UART and write the data:
                    uart.flush();
                    uart.write(cmd.msg, cmd.tx_length);
                    uart.flushTxTimed(response_ticks);
                    if (instance->_debug > 2) {
                        hex_msg(cmd.msg, "RS485 Tx: ", cmd.tx_length);
                    }

                    // Read the response
                    size_t read_length  = 0;
                    size_t current_read = uart.timedReadBytes(rx_message, cmd.rx_length, response_ticks);
                    read_length += current_read;
                    unresponsive = read_length != 0;

                    // Apparently some Huanyang report modbus errors in the correct way
                    // and others do not.  Check for the condition and truncate the first byte.
                    if (read_length > 0 && instance->_modbus_id != 0 && rx_message[0] == 0) {
                        log_debug("Huanyang workaround");
                        memmove(rx_message + 1, rx_message, read_length - 1);
                    }

                    if (instance->_debug > 2) {
                        hex_msg(rx_message, "RS485 Rx: ", read_length);
                    }

                    if (checkRx(cmd, rx_message, read_length, instance->_modbus_id)) {
                        // The response is well-formed

                        // Parse it if we have a parser
                        if (parser != nullptr) {
                            if (parser(rx_message, instance, impl)) {
                                // If we're initializing, move to the next initialization command:
                                if (pollidx < 0) {
                                    --pollidx;
                                }
                            } else {
                                log_debug("Parse Failed");

                                // Restart the init sequence
                                pollidx = -1;
                            }
                        }
                        retry_count = instance->_retries + 1;  // stop retry'ing
                    } else {
                        // Wait a bit before we retry.
                        delay_ms(instance->_poll_ms);

#ifdef DEBUG_TASK_STACK
                        static UBaseType_t uxHighWaterMark = 0;
                        reportTaskStackSize(uxHighWaterMark);
#endif
                    }
                }

#if 0
                if (retry_count == instance->_retries) {
                    if (!unresponsive) {
                        log_info("VFD RS485 Unresponsive");
                        unresponsive = true;
                        pollidx      = -1;
                    }
                    if (cmd.critical) {
                        mc_critical(ExecAlarm::SpindleControl);
                        log_error("Critical VFD RS485 Unresponsive");
                    }
                }
#endif
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
            if (speed == spindle->_current_dev_speed) {  // prevent setting same speed twice
                return false;
            }
            spindle->_current_dev_speed = speed;

            // Do variant-specific command preparation
            set_speed_command(speed, data);

#if 0
            // Sometimes sync_dev_speed is retained between different set_speed_command's. We don't want that - we want
            // spindle sync to kick in after we set the speed. This forces that.
            spindle->_sync_dev_speed = UINT32_MAX;
#endif

            return true;
        }

        // Calculate the CRC on all of the byte except the last 2
        // It then added the CRC to those last 2 bytes
        // full_msg_len This is the length of the message including the 2 crc bytes
        // Source: https://ctlsys.com/support/how_to_compute_the_modbus_rtu_message_crc/
        uint16_t VFDProtocol::ModRTU_CRC(uint8_t* buf, size_t msg_len) {
            uint16_t crc = 0xFFFF;
            for (size_t pos = 0; pos < msg_len; pos++) {
                crc ^= uint16_t(buf[pos]);  // XOR byte into least sig. byte of crc.

                for (size_t i = 8; i != 0; i--) {  // Loop over each bit
                    if ((crc & 0x0001) != 0) {     // If the LSB is set
                        crc >>= 1;                 // Shift right and XOR 0xA001
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
