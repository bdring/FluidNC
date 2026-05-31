// Copyright 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TelnetClient.h"
#include "TelnetServer.h"

#include "../System.h"     // inMotionState()

#include <WiFi.h>
#include <lwip/sockets.h>  // ::send(), MSG_DONTWAIT
#include <errno.h>

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

    // Send `len` bytes, never blocking long enough to risk the task watchdog.
    void TelnetClient::sendBuffered(const uint8_t* data, size_t len, int sockfd, int wait_ms) {
        size_t off    = 0;
        int    waited = 0;
        while (off < len) {
            int n = ::send(sockfd, data + off, len - off, MSG_DONTWAIT);
            if (n > 0) {
                off += (size_t)n;
            } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (waited >= wait_ms) {
                    break;  // send buffer still full — drop the rest
                }
                delay(1);
                ++waited;
            } else {
                _wifiClient->stop();  // hard error / peer gone
                closeOnDisconnect();
                return;
            }
        }

        if (off < len) {  // could not fully deliver this line
            if (++_txStallCount >= TX_STALL_LIMIT) {
                _wifiClient->stop();  // wedged peer — drop it
                closeOnDisconnect();  // connected() now false -> reaped in poll()
            }
        } else {
            _txStallCount = 0;
        }
    }

    size_t TelnetClient::write(const uint8_t* buffer, size_t length) {
        if (_state == -1 || buffer == nullptr || length == 0) {
            return length;
        }
        int sockfd = _wifiClient->fd();
        if (sockfd < 0) {
            closeOnDisconnect();
            return length;
        }

        // Accumulate \r\n-normalized output until a complete line is obtained and
        // prevent emitting a partial line
        for (size_t i = 0; i < length; i++) {
            uint8_t c = buffer[i];
            if (c == '\n' && _txLastChar != '\r') {
                _txLine.push_back('\r');
            }
            _txLastChar = c;
            _txLine.push_back((char)c);
        }
        if (_txLine.empty() || _txLine.back() != '\n') {
            return length;  // wait for the rest of the line
        }

        // Idle: briefly wait for room so large responses survive a slow client.
        // In motion: don't wait - drop instead, so jogging/running never stalls.
        int wait_ms = inMotionState() ? 0 : TX_IDLE_WAIT_MS;
        sendBuffered((const uint8_t*)_txLine.data(), _txLine.size(), sockfd, wait_ms);

        _txLine.clear();
        _txLastChar = '\0';
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
