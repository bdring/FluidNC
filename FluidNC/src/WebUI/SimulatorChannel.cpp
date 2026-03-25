// Copyright (c) 2024 - Simulator Channel implementation
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "SimulatorChannel.h"
#include "WSChannel.h"
#include "Driver/Console.h"
#include "../../posix/simulator_engine.h"


namespace WebUI {
    SimulatorChannel::SimulatorChannel(WSChannel* wsChannel) :
        Channel("simulator"), _wsChannel(wsChannel) {
        // Create semaphore for protecting _pending_acks access
        _ack_semaphore = xSemaphoreCreateMutex();
        if (_ack_semaphore == nullptr) {
            log_error_to(Console, "SimulatorChannel: Failed to create ACK semaphore");
        }
        log_info_to(Console, "SimulatorChannel created, linked to WebSocket client #" << wsChannel->id());
        simulator_attach_client();
    }

    SimulatorChannel::~SimulatorChannel() {
        simulator_detach_client();
        if (_ack_semaphore != nullptr) {
            vSemaphoreDelete(_ack_semaphore);
            _ack_semaphore = nullptr;
        }
        if (_wsChannel) {
            log_info_to(Console, "SimulatorChannel destroyed for WebSocket client #" << _wsChannel->id());
        }
    }

    size_t SimulatorChannel::write(uint8_t c) {
        return write(&c, 1);
    }

    size_t SimulatorChannel::write(const uint8_t* buffer, size_t size) {
        if (!_wsChannel || buffer == nullptr || size == 0) {
            return 0;
        }
        // Send data back through the WebSocket
        return _wsChannel->write(buffer, size);
    }

    Error SimulatorChannel::pollLine(char* line) {
        // SimulatorChannel flow control and position update loop:
        // 1. Dequeue position updates from simulator_engine when flow control allows (pending_acks < 2)
        // 2. Format messages as JSON and send to WebSocket client
        // 3. Increment pending_acks counter on send
        // 4. Extract incoming ACK messages from WebSocket queue
        // 5. Decrement pending_acks on ACK reception
        //
        // NOTE: this method returns NoData (never copies to output line parameter)
        // because flow control management happens internally, not via Protocol's line-based mechanism.

        if (!_wsChannel) {
            return Error::NoData;
        }

        // Phase 1: Dequeue and send position updates (respecting flow control)
        if (_pending_acks < 10) {
            queue_message_t msg;
            if (simulator_queue_dequeue(&msg)) {
                // Accumulate steps to get absolute position for each axis
                for (int i = 0; i < 6; i++) {
                    _position_steps[i] = msg.position.steps[i];
                }

                // Format position update as JSON message
                char json_buffer[256];
                int  len = snprintf(json_buffer,
                                   sizeof(json_buffer),
                                   "{\"type\":\"position\",\"steps\":[%d,%d,%d,%d,%d,%d],\"elapsed_us\":%llu,\"is_final\":%s}\n",
                                   _position_steps[0],
                                   _position_steps[1],
                                   _position_steps[2],
                                   _position_steps[3],
                                   _position_steps[4],
                                   _position_steps[5],
                                   (unsigned long long)msg.position.elapsed_us,
                                   msg.is_final ? "true" : "false");

                if (len > 0 && len < (int)sizeof(json_buffer)) {
                    // Send the position update message to the client
                    write((const uint8_t*)json_buffer, len);

                    // Increment flow control counter
                    if (_ack_semaphore && xSemaphoreTake(_ack_semaphore, portMAX_DELAY) == pdTRUE) {
                        _pending_acks++;
                        xSemaphoreGive(_ack_semaphore);
                    }
                }
            }
        }

        // Phase 2: Extract incoming ACK messages from WebSocket queue
        auto& q = _wsChannel->queue();

        while (q.size()) {
            uint8_t ch = q.front();
            q.pop();

            // Check for line ending
            if (ch == '\r' || ch == '\n') {
                // Found end of line - process accumulated message
                if (!_line_buffer.empty() && _line_buffer[0] == '{') {
                    // JSON message - check for ACK types
                    if (_line_buffer.find("steps_ack") != std::string::npos || _line_buffer.find("move_ack") != std::string::npos ||
                        _line_buffer.find("error") != std::string::npos) {
                        // This is an ACK message - decrement pending ack counter
                        if (_ack_semaphore && xSemaphoreTake(_ack_semaphore, portMAX_DELAY) == pdTRUE) {
                            if (_pending_acks > 0) {
                                _pending_acks--;
                            }
                            xSemaphoreGive(_ack_semaphore);
                        }
                    }
                }
                // Clear the buffer after processing the line
                _line_buffer.clear();
                continue;
            }

            // Accumulate character in internal buffer
            _line_buffer.push_back(ch);

            // Prevent buffer overflow
            if (_line_buffer.size() > 254) {
                log_warn_to(Console, "SimulatorChannel: Line buffer overflow, discarding");
                _line_buffer.clear();
            }
        }

        return Error::NoData;
    }

    int SimulatorChannel::read() {
        if (!_wsChannel) {
            return -1;
        }
        return _wsChannel->read();
    }

    int SimulatorChannel::available() {
        if (!_wsChannel) {
            return 0;
        }
        return _wsChannel->available();
    }
    }
