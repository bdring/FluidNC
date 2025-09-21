// Copyright (c) 2021 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "src/Channel.h"
#include "src/FileStream.h"
#include <list>

class AsyncWebServerRequest;
class AsyncWebServerResponse;

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

        void executeCommandBackground(const char *cmd);

        bool anyOutput() { return _buflen > 0; }

        void out(const char* s, const char* tag) override;
        void out(const std::string& s, const char* tag) override;
        void out_acked(const std::string& s, const char* tag) override;
        int copyBufferSafe(uint8_t *dest_buffer, size_t maxLen, size_t total);

        std::mutex              _xBufferLock;
        std::list<std::string> _cmds;

    private:
        bool                   _silent      = false;
        AsyncWebServerRequest* _request     = nullptr;
        static const size_t    BUFLEN       = 1; //48*1024;
        char                   *_buffer = nullptr; //[BUFLEN];
        size_t                 _buflen = 0;
        size_t                 _allocsize = 0;
        AsyncWebServerResponse *_response    = nullptr;
        FileStream             *_fs         = nullptr;
        bool                   _done        = false;
        TaskHandle_t  _background_task_handle=nullptr;
        static void background_task(void* pvParameters);
    };

    extern WebClient webClient;
}
