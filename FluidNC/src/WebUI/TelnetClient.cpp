// Copyright 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Platform.h"
#include "TelnetClient.h"
#include "TelnetServer.h"

#include <WiFi.h>

#if defined(ESP32)
// arduino-esp32's NetworkClient never overrides Print::availableForWrite() --
// it is hard-coded to return 0 ("a single write may block"), so gating sends
// on it means queued data never gets flushed. Send directly through a
// non-blocking socket call instead.
//
// This is opt-in for ESP32 specifically (rather than opt-out for everything
// else) because it depends on details of arduino-esp32's NetworkClient --
// a raw BSD socket fd and lwip/sockets.h. arduino-pico's WiFiClient
// (RP2040/RP2350) and the POSIX/Arduino-Emulator hosted build have neither,
// but both *do* implement availableForWrite() correctly, so they use the
// portable Client-API path below instead.
#define TELNET_RAW_SOCKET_SEND 1
#include <lwip/sockets.h>  // ::send(), MSG_DONTWAIT
#include <errno.h>
#endif

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

    void TelnetClient::closeOnDisconnectLocked() {
        if (!_wifiClient->connected()) {
            bool expected = false;
            if (_disconnected.compare_exchange_strong(expected, true)) {
                allChannels.kill(this);
            }
        }
    }

    void TelnetClient::closeOnDisconnect() {
        xSemaphoreTake(_wifiMutex, portMAX_DELAY);
        closeOnDisconnectLocked();
        xSemaphoreGive(_wifiMutex);
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

    // Attempts to send up to `len` bytes without blocking.
    // Returns the number of bytes actually sent (0 if the socket isn't ready
    // to accept more right now), or -1 if the connection has failed.
    // Assumes _wifiMutex is already held.
    int TelnetClient::trySend(const uint8_t* data, size_t len) {
#ifdef TELNET_RAW_SOCKET_SEND
        int sockfd = _wifiClient->fd();
        if (sockfd < 0) {
            return -1;
        }
        int sent = ::send(sockfd, data, len, MSG_DONTWAIT);
        if (sent >= 0) {
            return sent;
        }
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
#else
        size_t canSend = _wifiClient->availableForWrite();
        size_t toSend  = len < canSend ? len : canSend;
        if (toSend == 0) {
            return _wifiClient->connected() ? 0 : -1;
        }
        size_t sent = _wifiClient->write(data, toSend);
        if (sent == 0 && !_wifiClient->connected()) {
            return -1;
        }
        return (int)sent;
#endif
    }

    // Non-blocking drain of the queue. Bytes that can't be sent now stay queued
    // and go out on the next write().
    void TelnetClient::flushQueue() {
        xSemaphoreTake(_wifiMutex, portMAX_DELAY);
        while (_txTail != _txHead) {
            size_t contiguous = _txHead > _txTail ? _txHead - _txTail : TX_QUEUE_SIZE - _txTail;
            int    sent       = trySend(_txQueue.data() + _txTail, contiguous);
            if (sent > 0) {
                _txTail = (_txTail + (size_t)sent) % TX_QUEUE_SIZE;
                continue;
            }
            if (sent < 0) {
                // Hard error (e.g. connection reset) -- disconnect immediately,
                // independent of the stall counter below.
                _wifiClient->stop();
                closeOnDisconnectLocked();
                xSemaphoreGive(_wifiMutex);
                return;
            }
            break;  // sent == 0: socket not ready right now
        }

        if (_txTail == _txHead) {
            _txStallCount = 0;  // fully drained
        } else if (++_txStallCount >= TX_STALL_LIMIT) {
            // Connected but not draining what we send it -- wedged peer.
            // Force it out so the slot can be reused.
            _wifiClient->stop();
            closeOnDisconnectLocked();
        }
        xSemaphoreGive(_wifiMutex);
    }

    void TelnetClient::queueCompletedLine() {
        const auto* data = reinterpret_cast<const uint8_t*>(_txLine.data());
        size_t      len  = _txLine.size();

        if (isCriticalLine(_txLine)) {
            if (!queueLine(data, len, 0)) {
                xSemaphoreTake(_wifiMutex, portMAX_DELAY);
                _wifiClient->stop();
                closeOnDisconnectLocked();
                xSemaphoreGive(_wifiMutex);
            }
        } else {
            queueLine(data, len, TX_CRITICAL_RESERVE);
        }
    }

    size_t TelnetClient::write(const uint8_t* buffer, size_t length) {
        if (_state == -1 || buffer == nullptr || length == 0) {
            return length;
        }
        xSemaphoreTake(_wifiMutex, portMAX_DELAY);
        bool isConnected = _wifiClient->connected();
        xSemaphoreGive(_wifiMutex);
        if (!isConnected) {
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
        xSemaphoreTake(_wifiMutex, portMAX_DELAY);
        int ret = _wifiClient->peek();
        xSemaphoreGive(_wifiMutex);
        return ret;
    }

    int TelnetClient::available() {
        if (_disconnected.load()) {
            return 0;
        }
        xSemaphoreTake(_wifiMutex, portMAX_DELAY);
        int ret = _wifiClient->available();
        xSemaphoreGive(_wifiMutex);
        return ret;
    }

    int TelnetClient::rx_buffer_available() {
        return WIFI_CLIENT_READ_BUFFER_SIZE - available();
    }

    int TelnetClient::read(void) {
        if (_disconnected.load()) {
            return -1;
        }

        xSemaphoreTake(_wifiMutex, portMAX_DELAY);
        auto ret = _wifiClient->read();
        if (ret < 0) {
            // calling _wifiClient->connected() is expensive when the client is
            // connected because it calls recv() to double check, so we check
            // infrequently, only after quite a few reads have returned no data
            if (++_empty_reads >= DISCONNECT_CHECK_COUNTS) {
                _empty_reads = 0;
                closeOnDisconnectLocked();
            }
        } else {
            // Reset the counter if we have data
            _empty_reads = 0;
        }
        xSemaphoreGive(_wifiMutex);
        return ret;
    }

    TelnetClient::~TelnetClient() {
        delete _wifiClient;
        vSemaphoreDelete(_wifiMutex);
    }
}
