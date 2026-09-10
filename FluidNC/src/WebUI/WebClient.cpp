// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "WebClient.h"
#include "Driver/Console.h"
#include <ESPAsyncWebServer.h>  // RESPONSE_TRY_AGAIN
#include <algorithm>

namespace WebUI {
    WebClient::WebClient() : Channel("webclient") { _lock = xSemaphoreCreateMutex(); }

    WebClient::~WebClient() {
        if (_lock) {
            vSemaphoreDelete(_lock);
            _lock = nullptr;
        }
    }

    void WebClient::attachWS(bool silent) {
        _silent = silent;
        _done.store(false);
        xSemaphoreTake(_lock, portMAX_DELAY);
        _head = _tail = 0;
        xSemaphoreGive(_lock);
    }

    void WebClient::detachWS() {
        _silent = true;
        // Wait for the polling task to finish the command (ack() latches _done),
        // so the chunked-response lambda is never called against a channel that
        // is about to be reaped.  is_closing() covers the case where the kill
        // was processed first.
        while (!_done.load() && !is_closing()) {
            delay(1);
        }
    }

    void WebClient::deliverCommand(const char* cmd) {
        // Feed the base Channel input queue; pollChannels() -> pollLine() then
        // hands this line to execute_line() on the polling task.
        push(std::string(cmd));
        push(static_cast<uint8_t>('\n'));
    }

    void WebClient::ack(Error status) {
        if (status != Error::Ok && status != Error::Deferred) {
            log_debug_to(Console, "Web command error " << static_cast<int>(status));
        }
        _done.store(true);
    }

    size_t WebClient::write(const uint8_t* buffer, size_t length) {
        if (_silent || !length) {
            return length;
        }
        size_t written = 0;
        // Bounded backpressure: give the HTTP side a short window to drain a
        // full ring, then drop the rest rather than stall the polling task.
        for (int spins = 0; written < length; ) {
            xSemaphoreTake(_lock, portMAX_DELAY);
            while (written < length && _count() < BUFLEN - 1) {
                _buffer[_head] = static_cast<char>(buffer[written++]);
                _head          = (_head + 1) % BUFLEN;
            }
            xSemaphoreGive(_lock);
            if (written == length) {
                break;
            }
            if (_silent || ++spins > 500) {  // ~500 ms; client not reading
                break;
            }
            delay(1);
        }
        return length;
    }

    size_t WebClient::write(uint8_t data) { return write(&data, 1); }

    size_t WebClient::copyBufferSafe(uint8_t* dest_buffer, size_t maxLen, size_t total) {
        xSemaphoreTake(_lock, portMAX_DELAY);
        size_t n = std::min(_count(), maxLen);
        for (size_t i = 0; i < n; ++i) {
            dest_buffer[i] = static_cast<uint8_t>(_buffer[_tail]);
            _tail          = (_tail + 1) % BUFLEN;
        }
        xSemaphoreGive(_lock);

        if (n) {
            return n;
        }
        if (_done.load() || _silent || is_closing()) {
            return 0;  // end of stream
        }
        return RESPONSE_TRY_AGAIN;  // command still running, nothing buffered yet
    }
}
