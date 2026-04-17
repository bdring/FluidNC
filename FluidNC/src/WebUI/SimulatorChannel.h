// Copyright (c) 2024 - Simulator Channel for Machine Simulator integration
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Channel.h"
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace WebUI {
    class WSChannel;

    // SimulatorChannel is a Channel that receives simulator protocol messages
    // from a web-based CNC machine simulator via a WebSocket connection.
    // When the simulator connects via WSChannel with {independent_session: true},
    // a SimulatorChannel instance is created to bridge the communication.
    //
    // SimulatorChannel performs bidirectional communication:
    // - Inbound (pollLine): Receives ACK messages and manages flow control
    // - Outbound: Broadcasts position updates from the simulator engine to the client
    class SimulatorChannel : public Channel {
    private:
        WSChannel* _wsChannel;
        
        // Internal line buffer for accumulated simulator messages
        std::string _line_buffer;
        
        // ACK tracking for flow control (1-2 messages in flight)
        SemaphoreHandle_t _ack_semaphore = nullptr;
        int               _pending_acks  = 0;  // Number of messages sent but not yet acked (0, 1, or 2)
        
        // Position tracking (accumulates differential steps to maintain absolute position)
        int32_t _position_steps[6] = {0};  // Current absolute position in steps for each axis

    public:
        SimulatorChannel(WSChannel* wsChannel);
        virtual ~SimulatorChannel();

        // Override Channel methods
        size_t write(uint8_t c) override;
        size_t write(const uint8_t* buffer, size_t size) override;
        Error  pollLine(char* line) override;
        int    read() override;
        int    available() override;
        void   flush(void) override {}

        WSChannel* getWSChannel() const { return _wsChannel; }
    };
}
