// Copyright (c) 2021 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Channel.h"
#include <freertos/semphr.h>

class AsyncWebServerRequest;
class AsyncWebServerResponse;

namespace WebUI {
    // WebClient is a transient Channel that backs one HTTP GET /command request.
    //
    // Old design (commit 508850c): a dedicated FreeRTOS task drained a queue of
    // WebClient* and ran settings_execute_line() on each, so the AsyncTCP task
    // would not block on a long command ([ESP400] emits >33 KB of JSON) and the
    // response could be streamed instead of buffered whole.
    //
    // New design: WebClient is registered in allChannels like any other channel.
    // The polling task picks up the queued command line via the normal
    // pollChannels() -> execute_line() path - the same one that runs an
    // [ESP400] typed on a telnet console - and the command's output lands in a
    // small bounded buffer that the chunked HTTP response drains.  No extra
    // task, no bespoke queue.
    class WebClient : public Channel {
    public:
        WebClient();
        ~WebClient();

        // Marks this response quiet: output is discarded (used for
        // /command_silent, and after the HTTP client has gone away).
        void attachWS(bool silent);
        // Called from the request's onDisconnect: stop accepting output and
        // wait for any in-flight command to finish so the chunked-response
        // lambda is never invoked against a half-torn-down channel.
        void detachWS();

        // Hand the command line to the polling task (pushes it into the base
        // Channel input queue; pollLine() then delivers it).
        void deliverCommand(const char* cmd);

        size_t write(uint8_t data) override;
        size_t write(const uint8_t* buffer, size_t length) override;
        void   flush() override {}

        // The [ESPxxx] response body IS the command's own output - suppress the
        // "ok"/"error:" line the base ack() would add, just latch end-of-stream.
        void ack(Error status) override;

        void sendLine(MsgLevel level, const char* line) override { print_msg(level, line); }
        void sendLine(MsgLevel level, const std::string* line) override {
            print_msg(level, line->c_str());
            delete line;
        }
        void sendLine(MsgLevel level, const std::string& line) override { print_msg(level, line.c_str()); }

        void out(const char* s, const char* tag) override { write(reinterpret_cast<const uint8_t*>(s), strlen(s)); }
        void out(const std::string& s, const char* tag) override { write(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
        void out_acked(const std::string& s, const char* tag) override { out(s, tag); }

        // Called by the chunked-response lambda on the AsyncTCP task.  Returns
        // bytes copied, 0 for end-of-stream, or RESPONSE_TRY_AGAIN while the
        // command is still running with nothing buffered yet.
        size_t copyBufferSafe(uint8_t* dest_buffer, size_t maxLen, size_t total);

    private:
        SemaphoreHandle_t _lock    = nullptr;
        bool              _silent  = false;  // discard output
        std::atomic<bool> _done { false };   // command finished (or channel closing)

        // Bounded output ring.  The point of the streaming design is to *not*
        // hold a whole [ESP400] response; when full, write() briefly waits for
        // the HTTP side to drain, then drops the overflow.
        static constexpr size_t BUFLEN = 2048;
        char                    _buffer[BUFLEN];
        size_t                  _head = 0;
        size_t                  _tail = 0;
        size_t                  _count() const { return (_head + BUFLEN - _tail) % BUFLEN; }
    };
}
