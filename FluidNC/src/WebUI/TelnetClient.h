// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Channel.h"

#include <WiFi.h>
#include <string>

namespace WebUI {
    class TelnetClient : public Channel {
        WiFiClient* _wifiClient;

        // The default value of the rx buffer in WiFiClient.cpp is 1436 which is
        // related to the network frame size minus TCP/IP header sizes.
        // The WiFiClient API has no way to override or query it.
        // We use a smaller value for safety.  There is little advantage
        // to sending too many GCode lines at once, especially since the
        // common serial communication case is typically limited to 128 bytes.
        static const int WIFI_CLIENT_READ_BUFFER_SIZE = 1200;

        static const int DISCONNECT_CHECK_COUNTS = 1000;

        // Disconnect clients that repeatedly stall writes, so a dead peer can't
        // block FluidNC's write path and trip the task watchdog.
        static const int TX_STALL_LIMIT = 10;
        static const int TX_IDLE_WAIT_MS = 50;

        int32_t     _state        = 0;
        int         _txStallCount = 0;
        std::string _txLine;            // output accumulated until a full line
        uint8_t     _txLastChar   = '\0';  // for \n -> \r\n across write() calls

        void sendBuffered(const uint8_t* data, size_t len, int sockfd, int wait_ms);

    public:
        TelnetClient(WiFiClient* wifiClient);

        int    rx_buffer_available() override;
        size_t write(uint8_t data) override;
        size_t write(const uint8_t* buffer, size_t size) override;
        int    read(void) override;
        int    peek(void) override;
        int    available() override;
        void   flush() override {}
        void   flushRx() override;

        void closeOnDisconnect();

        void handle() override;

        ~TelnetClient();
    };
}
