// Copyright (c) 2021 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "src/Channel.h"

class AsyncWebServerRequest;
class AsyncResponseStream;

namespace WebUI {
    class WebClient : public Channel {
    public:
        WebClient();
        ~WebClient();

        void attachWS(AsyncWebServerRequest* request, bool silent);
        void detachWS();

        size_t write(uint8_t data) override;
        size_t write(const uint8_t* buffer, size_t length) override;
        void   flush();

        void sendLine(MsgLevel level, const char* line) override;
        void sendLine(MsgLevel level, const std::string* line) override;
        void sendLine(MsgLevel level, const std::string& line) override;

        void sendError(int code, const std::string& line);

        bool anyOutput() { return _header_sent; }

        void out(const char* s, const char* tag) override;
        void out(const std::string& s, const char* tag) override;
        void out_acked(const std::string& s, const char* tag) override;

    private:
        bool                   _header_sent = false;
        bool                   _silent      = false;
        AsyncWebServerRequest* _request     = nullptr;
        static const size_t    BUFLEN       = 1200;
        char                   _buffer[BUFLEN];
        size_t                 _buflen = 0;
        AsyncResponseStream     *_response    = nullptr;
    };

    extern WebClient webClient;
}
