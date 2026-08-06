// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Channel.h"

// For the POSIX Arduino-Emulator core, Arduino.h must come before WiFi.h
// so WiFi symbols are visible without arduino:: qualification.
#include <Arduino.h>
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

        static const int DISCONNECT_CHECK_COUNTS = 10;

        std::atomic<bool> _disconnected { false };
        int32_t           _empty_reads = 0;

        // Guards all access to _wifiClient. write()/flushQueue() run on the
        // output task while read()/peek()/available()/closeOnDisconnect() run
        // on the polling task; WiFiClient has no internal locking of its own,
        // so without this a stop() on one task could race a fd()/send() (or
        // similar) on the other and hit a socket fd that has already been
        // closed and reissued to an unrelated connection.
        mutable SemaphoreHandle_t _wifiMutex = xSemaphoreCreateMutex();

        // Outgoing telnet bytes are buffered as whole lines in this ring,
        // momentarilly full socket shall never truncates a line or drop an ack.
        static constexpr size_t TX_QUEUE_SIZE       = 2304;
        static constexpr size_t TX_CRITICAL_RESERVE = 256;

        // A TCP peer can stay "connected" (no FIN/RST) while no longer
        // draining what we send it -- a frozen terminal, a dead network path,
        // a zero TCP window. That alone doesn't fill the ring buffer's
        // TX_CRITICAL_RESERVE headroom, so it isn't caught by queueLine()'s
        // critical-line disconnect. Count consecutive flushQueue() calls that
        // fail to fully drain the backlog and force the client out once it's
        // wedged for TX_STALL_LIMIT of them in a row, freeing the slot.
        static const int TX_STALL_LIMIT = 10;
        int              _txStallCount  = 0;

        int32_t     _state = 0;
        std::string _txLine;             // output accumulated until a full line
        uint8_t     _txLastChar = '\0';  // for \n -> \r\n across write() calls

        std::array<uint8_t, TX_QUEUE_SIZE> _txQueue {};
        size_t                             _txHead = 0;
        size_t                             _txTail = 0;

        static bool isCriticalLine(const std::string& line);
        size_t      queueFree() const;
        bool        queueLine(const uint8_t* data, size_t len, size_t reserve);
        int         trySend(const uint8_t* data, size_t len);
        void        flushQueue();
        void        queueCompletedLine();

        // Core of closeOnDisconnect(), assumes _wifiMutex is already held.
        void closeOnDisconnectLocked();

    public:
        TelnetClient(WiFiClient* wifiClient);

        int    rx_buffer_available() override;
        size_t write(uint8_t data) override;
        size_t write(const uint8_t* buffer, size_t size) override;
        int    read(void) override;
        int    peek(void) override;
        int    available() override;
        void   flushRx() override;

        void closeOnDisconnect();

        void handle() override;

        ~TelnetClient();
    };
}
