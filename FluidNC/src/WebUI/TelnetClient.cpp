// Copyright 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TelnetClient.h"
#include "TelnetServer.h"

#include <WiFi.h>

namespace WebUI {
    TelnetClient::TelnetClient(WiFiClient* wifiClient) : Channel("telnet"), _wifiClient(wifiClient) {}

    void TelnetClient::handle() {}

    void TelnetClient::closeOnDisconnect() {
        if (_state != -1 && !_wifiClient->connected()) {
            _state = -1;
            TelnetServer::_disconnected.push(this);
        }
    }

    void TelnetClient::flushRx() {
        Channel::flushRx();
    }

    size_t TelnetClient::write(uint8_t data) {
        return write(&data, 1);
    }

    size_t TelnetClient::write(const uint8_t* buffer, size_t length) {
        // Replace \n with \r\n
        size_t  rem      = length;
        uint8_t lastchar = '\0';
        size_t  j        = 0;
        while (rem) {
            const int bufsize = 128;
            uint8_t   modbuf[bufsize];
            // bufsize-1 in case the last character is \n
            size_t k = 0;
            while (rem && k < (bufsize - 1)) {
                uint8_t c = buffer[j++];
                if (c == '\n' && lastchar != '\r') {
                    modbuf[k++] = '\r';
                }
                lastchar    = c;
                modbuf[k++] = c;
                --rem;
            }
            if (k) {
                auto nWritten = _wifiClient->write(modbuf, k);
                if (nWritten == 0) {
                    closeOnDisconnect();
                }
            }
        }
        return length;
    }

    int TelnetClient::peek(void) {
        return _wifiClient->peek();
    }

    int TelnetClient::available() {
        return _wifiClient->available();
    }

    int TelnetClient::rx_buffer_available() {
        return WIFI_CLIENT_READ_BUFFER_SIZE - available();
    }

    int TelnetClient::read(void) {
        if (_state == -1) {
            return -1;
        }
        auto ret = _wifiClient->read();
        if (ret < 0) {
            // calling _wifiClient->connected() is expensive when the client is
            // connected because it calls recv() to double check, so we check
            // infrequently, only after quite a few reads have returned no data
            if (++_state >= DISCONNECT_CHECK_COUNTS) {
                _state = 0;
                closeOnDisconnect();  // sets _state to -1 if disconnected
            }
        } else {
            // Reset the counter if we have data
            _state = 0;
        }
        return ret;
    }

    TelnetClient::~TelnetClient() {
        delete _wifiClient;
    }
}
