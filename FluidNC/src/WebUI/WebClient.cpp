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
        _silent.store(silent);
        _done.store(false);
        _aborted.store(false);
        xSemaphoreTake(_lock, portMAX_DELAY);
        _head = _tail = 0;
        xSemaphoreGive(_lock);
    }

    void WebClient::detachWS() {
        _silent.store(true);
        // Wait briefly for the polling task to finish the command (ack() latches
        // _done) so the chunked-response callback is not mid-run against a
        // channel about to be reaped - but never block forever.  An aborted
        // command skips ack(), and kill() (which sets is_closing()) is only
        // queued after this returns; the reaper still gates the actual delete
        // on the processing reference, so proceeding after the timeout is safe.
        for (int i = 0; i < 200 && !_done.load() && !is_closing(); ++i) {
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
        if (_silent.load() || _aborted.load() || !length) {
            return length;
        }
        size_t written = 0;
        // Bounded backpressure: give the HTTP side a short window to drain a
        // full ring, then give up rather than stall the polling task.
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
            if (_silent.load()) {
                break;
            }
            if (++spins > 500) {  // ~500 ms and the client still is not reading
                // Latch the response as truncated: copyBufferSafe() ends the
                // stream after the ring drains so the client sees a short
                // (broken) body rather than a hang, and further output is
                // dropped fast.
                _aborted.store(true);
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
        if (_done.load() || is_closing() || _aborted.load()) {
            return 0;  // end of stream: command finished, channel closing, or output truncated
        }
        // _silent alone is not end-of-stream - a silent command still runs to
        // completion and its ack() latches _done.
        return RESPONSE_TRY_AGAIN;  // command still running, nothing buffered yet
    }
}
