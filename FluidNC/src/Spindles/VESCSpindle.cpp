// Copyright (c) 2022 -	Lukas Goßmann (GitHub: LukasGossmann)
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "VESCSpindle.h"

#include "../crc16_ccitt.h"    // crc16_ccitt
#include "../MotionControl.h"  // mc_reset
#include "../Protocol.h"       // rtAlarm

#include <esp_attr.h>  // IRAM_ATTR
#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>

const int        VESC_UART_QUEUE_SIZE    = 10;   // number of commands that can be queued up.
const int        VESC_UART_POLL_RATE     = 500;  // in milliseconds between commands
const TickType_t VESC_UART_RESPONSE_WAIT = 200;  // in milliseconds

namespace Spindles {

    QueueHandle_t VESC::_vesc_cmd_queue     = nullptr;
    TaskHandle_t  VESC::_vesc_cmdTaskHandle = nullptr;

    void VESC::vesc_cmd_task(void* pvParameters) {
        VESC* instance = static_cast<VESC*>(pvParameters);
        auto& uart     = *instance->_uart;

        uint8_t  speedCommandBuffer[10];
        uint32_t speedCommandLength = 0;

        // https://github.com/vedderb/bldc/blob/553548a6e2145dd5df14e62a4e40a41b8a5c0334/comm/commands.c#L366
        const uint32_t value_selector_fault = 1 << 15;
        uint8_t        statusCommandBuffer[10];
        int            statusCommandLength = create_command(
            comm_packet_id::COMM_GET_VALUES_SELECTIVE, value_selector_fault, sizeof(statusCommandBuffer), statusCommandBuffer);

        for (; true; delay_ms(VESC_UART_POLL_RATE)) {
            std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // read fence for settings

            vesc_action action;
            if (xQueueReceive(_vesc_cmd_queue, &action, 0)) {
                speedCommandLength =
                    create_command((comm_packet_id)action.mode, action.value, sizeof(speedCommandBuffer), speedCommandBuffer);
            }

            if (speedCommandLength > 0) {
                // VESC needs to be sent the speed command the entire time it should run.
                // Otherwise it times out and stops running.
                // So we just keep repeating the last command forever.
                // This is intended behaviour!
                uart.write(speedCommandBuffer, speedCommandLength);
            }

            uart.write(statusCommandBuffer, statusCommandLength);
            uart.flush();

            uint8_t readBuffer[11];
            size_t  numBytesRead = uart.timedReadBytes(readBuffer, sizeof(readBuffer), VESC_UART_RESPONSE_WAIT);
            if (numBytesRead != sizeof(readBuffer)) {
                log_warn("VESC: Did not receive the number of bytes expected! (" << numBytesRead << " insted of " << sizeof(readBuffer)
                                                                                 << ")");
                continue;
            }

            uint8_t faultCode = 0;
            bool    success   = parse_fault_code_response(faultCode, numBytesRead, readBuffer);

            if (!success) {
                log_error("VESC: Failed to parse fault code response!");
                continue;
            }

            if (faultCode != mc_fault_code::FAULT_CODE_NONE) {
                log_error("VESC: Fault detected! (" << faultCode << ")");

                mc_reset();
                rtAlarm = ExecAlarm::SpindleControl;

                speedCommandLength = 0;
                memset(speedCommandBuffer, 0, sizeof(speedCommandBuffer));

                continue;
            }
        }
    }

    int VESC::create_command(comm_packet_id packetId, int value, size_t buffLen, uint8_t* buffer) {
        if (buffLen < 10)
            return 0;

        // https://github.com/vedderb/bldc/blob/b900ffcde534780842c581b76ceaa44c202c6054/comm/packet.c#L155
        buffer[0] = 2;                  // Start byte
        buffer[1] = 5;                  // Packet length (packet id + value)
        buffer[2] = (uint8_t)packetId;  // Packet id

        buffer[3] = (value & 0xFF000000) >> 24;
        buffer[4] = (value & 0x00FF0000) >> 16;
        buffer[5] = (value & 0x0000FF00) >> 8;
        buffer[6] = (value & 0x000000FF);

        uint16_t crc = crc16_ccitt(buffer + 2, 5);

        buffer[7] = ((crc & 0xFF00) >> 8);  // CRC
        buffer[8] = (crc & 0x00FF);         // CRC
        buffer[9] = 3;                      // Stop byte

        return 10;
    }

    bool VESC::parse_fault_code_response(uint8_t& faultCode, size_t readByteCount, uint8_t* buffer) {
        // https://github.com/vedderb/bldc/blob/b900ffcde534780842c581b76ceaa44c202c6054/comm/packet.c#L41
        // [0] = Start byte (2)
        // [1] = Length of payload (16)
        // [2] = Packet Id: COMM_PACKET_ID::COMM_GET_VALUES_SELECTIVE
        // [3] = Mask (echo)
        // [4] = Mask (echo)
        // [5] = Mask (echo)
        // [6] = Mask (echo)
        // [7] = Fault mask
        // [8] = CRC
        // [9] = CRC
        // [10] = Stop byte (3)

        if (readByteCount != 11) {
            return false;
        }

        // Check start and stop byte
        if (buffer[0] != 2 || buffer[10] != 3) {
            return false;
        }

        if (buffer[2] != comm_packet_id::COMM_GET_VALUES_SELECTIVE) {
            return false;
        }

        uint16_t crc         = crc16_ccitt(buffer + 2, 6);
        uint16_t receivedCRC = ((uint16_t)buffer[8]) << 8 | buffer[9];
        if (crc != receivedCRC) {
            return false;
        }

        faultCode = buffer[7];

        return true;
    }

    void VESC::init() {
        _uart->begin();

        if (_uart->setHalfDuplex()) {
            log_warn("VESC: RS485 UART set half duplex failed");
            return;
        }

        is_reversable  = true;
        _current_state = SpindleState::Disable;

        setupSpeeds(maxSpeed());

        if (!_vesc_cmd_queue) {
            _vesc_cmd_queue = xQueueCreate(VESC_UART_QUEUE_SIZE, sizeof(vesc_action));
            xTaskCreatePinnedToCore(vesc_cmd_task,         // task
                                    "vesc_cmdTaskHandle",  // name for task
                                    2048,                  // size of task stack
                                    this,                  // parameters
                                    1,                     // priority
                                    &_vesc_cmdTaskHandle,
                                    SUPPORT_TASK_CORE  // core
            );
        }

        config_message();

        set_state_internal(SpindleState::Disable, 0, false);
    }

    void VESC::config_message() {
        _uart->config_message(name(), " Spindle ");
    }

    void VESC::set_state_internal(SpindleState state, SpindleSpeed speed, bool fromISR) {
        if (_vesc_cmd_queue) {
            vesc_action action;
            uint32_t    mappedValue = mapSpeed(speed);
            uint32_t    scaledValue = 0;

            switch (_control_mode_to_use) {
                case vesc_control_mode::CURRENT: {
                    // Scaling factor is based on the value ranges used here and not within vesc!
                    scaledValue = mappedValue * 10;

                } break;

                case vesc_control_mode::DUTY: {
                    // Scaling factor is based on the value ranges used here and not within vesc!
                    scaledValue = mappedValue * 1000;
                } break;

                case vesc_control_mode::RPM: {
                    // VESC uses ERPM to drive the motor. RPM = ERPM / _number_of_pole_pairs
                    scaledValue = mappedValue * _number_of_pole_pairs;
                } break;
            }

            switch (state) {
                default:
                // For disable current mode is used otherwise the motor will still be energized while its not spinning.
                case SpindleState::Disable: {
                    action.value = 0;
                    action.mode  = vesc_control_mode::CURRENT;
                } break;

                case SpindleState::Cw: {
                    action.value = scaledValue;
                    action.mode  = (vesc_control_mode)_control_mode_to_use;
                } break;

                case SpindleState::Ccw: {
                    action.value = -scaledValue;
                    action.mode  = (vesc_control_mode)_control_mode_to_use;
                } break;
            }

            if (fromISR) {
                if (xQueueSendFromISR(_vesc_cmd_queue, &action, 0) != pdTRUE) {
                    log_warn("VESC Queue Full");
                }
            } else {
                if (xQueueSend(_vesc_cmd_queue, &action, 0) != pdTRUE) {
                    log_warn("VESC Queue Full");
                }
            }

            _last_spindle_state = state;
            _last_spindle_speed = speed;
        }
    }

    void VESC::setState(SpindleState state, SpindleSpeed speed) {
        if (sys.abort) {
            return;  // Block during abort.
        }

        if (_last_spindle_state == state && _last_spindle_speed == speed) {
            return;
        }

        set_state_internal(state, speed, false);

        spindleDelay(state, speed);
    }

    void IRAM_ATTR VESC::setSpeedfromISR(uint32_t dev_speed) {
        if (_last_spindle_speed == dev_speed) {
            return;
        }

        set_state_internal(_current_state, dev_speed, true);
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<VESC> registration("VESC");
    }
}
