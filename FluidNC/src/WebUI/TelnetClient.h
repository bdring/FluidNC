// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Channel.h"

#include <array>
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

        // Outgoing telnet bytes are buffered as whole lines in this ring,
        // momentarilly full socket shall never truncates a line or drop an ack.
        static constexpr size_t TX_QUEUE_SIZE       = 2304;
        static constexpr size_t TX_CRITICAL_RESERVE = 256;

        int32_t     _state      = 0;
        std::string _txLine;               // output accumulated until a full line
        uint8_t     _txLastChar = '\0';    // for \n -> \r\n across write() calls

        std::array<uint8_t, TX_QUEUE_SIZE> _txQueue {};
        size_t                             _txHead = 0;
        size_t                             _txTail = 0;

        static bool isCriticalLine(const std::string& line);
        size_t      queueFree() const;
        bool        queueLine(const uint8_t* data, size_t len, size_t reserve);
        void        flushQueue(int sockfd);
        void        queueCompletedLine();

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
