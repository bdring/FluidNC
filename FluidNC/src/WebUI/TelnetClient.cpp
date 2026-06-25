// Copyright 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Platform.h"
#include "TelnetClient.h"
#include "TelnetServer.h"

#include <WiFi.h>

namespace WebUI {
    TelnetClient::TelnetClient(WiFiClient* wifiClient) : Channel("telnet"), _wifiClient(wifiClient) {
#if !HOSTED
        _wifiClient->setNoDelay(true);
#endif
#ifdef __FLUIDNC_RP2040_H__
        _wifiClient->setSync(false);
#endif
    }

    void TelnetClient::handle() {}

    void TelnetClient::closeOnDisconnect() {
        if (!_wifiClient->connected()) {
            bool expected = false;
            if (_disconnected.compare_exchange_strong(expected, true)) {
                allChannels.kill(this);
            }
        }
    }

    void TelnetClient::flushRx() {
        Channel::flushRx();
    }

    size_t TelnetClient::write(uint8_t data) {
        return write(&data, 1);
    }

    // Prevent dropping of critical command ack
    bool TelnetClient::isCriticalLine(const std::string& line) {
        return line == "ok\r\n" || line.rfind("error:", 0) == 0 || line.rfind("ALARM:", 0) == 0 || line.rfind("[MSG:ERR:", 0) == 0;
    }

    size_t TelnetClient::queueFree() const {
        size_t used = (_txHead + TX_QUEUE_SIZE - _txTail) % TX_QUEUE_SIZE;
        return TX_QUEUE_SIZE - used - 1;
    }

    bool TelnetClient::queueLine(const uint8_t* data, size_t len, size_t reserve) {
        if (len + reserve > queueFree()) {
            return false;
        }
        for (size_t i = 0; i < len; ++i) {
            _txQueue[_txHead] = data[i];
            _txHead           = (_txHead + 1) % TX_QUEUE_SIZE;
        }
        return true;
    }

    // Non-blocking drain of the queue. Bytes that can't be sent now stay queued
    // and go out on the next write().
    void TelnetClient::flushQueue() {
        while (_txTail != _txHead) {
            size_t contiguous = _txHead > _txTail ? _txHead - _txTail : TX_QUEUE_SIZE - _txTail;
            size_t canSend    = _wifiClient->availableForWrite();
            size_t toSend     = contiguous < canSend ? contiguous : canSend;
            if (toSend == 0) {
                return;  // nothing can be sent right now
            }
            size_t sent = _wifiClient->write(_txQueue.data() + _txTail, toSend);
            if (sent > 0) {
                _txTail = (_txTail + sent) % TX_QUEUE_SIZE;
                continue;
            }
            if (!_wifiClient->connected()) {
                _wifiClient->stop();  // hard error / peer gone
                closeOnDisconnect();
            }
            return;
        }
    }

    void TelnetClient::queueCompletedLine() {
        const auto* data = reinterpret_cast<const uint8_t*>(_txLine.data());
        size_t      len  = _txLine.size();

        if (isCriticalLine(_txLine)) {
            if (!queueLine(data, len, 0)) {
                _wifiClient->stop();
                closeOnDisconnect();
            }
        } else {
            queueLine(data, len, TX_CRITICAL_RESERVE);
        }
    }

    size_t TelnetClient::write(const uint8_t* buffer, size_t length) {
        if (_state == -1 || buffer == nullptr || length == 0) {
            return length;
        }
        if (_wifiClient->connected()) {
            closeOnDisconnect();
            return length;
        }

        // Accumulate \r\n-normalized output AND queue each complete line. A single
        // write() may carry more than one line. Handle boundaries as they come.
        for (size_t i = 0; i < length; i++) {
            uint8_t c = buffer[i];
            if (c == '\n' && _txLastChar != '\r') {
                _txLine.push_back('\r');
            }
            _txLastChar = c;
            _txLine.push_back((char)c);
            if (c == '\n') {
                queueCompletedLine();
                _txLine.clear();
                _txLastChar = '\0';
                if (_state == -1) {
                    return length;
                }
            }
        }

        flushQueue();
        return length;
    }

    int TelnetClient::peek(void) {
        if (_disconnected.load()) {
            return -1;
        }
        return _wifiClient->peek();
    }

    int TelnetClient::available() {
        if (_disconnected.load()) {
            return 0;
        }
        return _wifiClient->available();
    }

    int TelnetClient::rx_buffer_available() {
        return WIFI_CLIENT_READ_BUFFER_SIZE - available();
    }

    int TelnetClient::read(void) {
        if (_disconnected.load()) {
            return -1;
        }

        auto ret = _wifiClient->read();
        if (ret < 0) {
            // calling _wifiClient->connected() is expensive when the client is
            // connected because it calls recv() to double check, so we check
            // infrequently, only after quite a few reads have returned no data
            if (++_empty_reads >= DISCONNECT_CHECK_COUNTS) {
                _empty_reads = 0;
                if (!_wifiClient->connected()) {
                    closeOnDisconnect();
                }
            }
        } else {
            // Reset the counter if we have data
            _empty_reads = 0;
        }
        return ret;
    }

    TelnetClient::~TelnetClient() {
        delete _wifiClient;
    }
}
