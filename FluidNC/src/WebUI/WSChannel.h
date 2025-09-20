// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstdint>
#include <cstring>
#include <list>
#include <map>
#include <ESPAsyncWebServer.h>

//class AsyncWebSocket;
//class AsyncWebSocketClient;

#include "src/Channel.h"

namespace WebUI {
    class WSChannel : public Channel {
    public:
        WSChannel(AsyncWebSocket* server, uint32_t clientNum);

        size_t write(uint8_t c);
        size_t write(const uint8_t* buffer, size_t size);

        bool sendTXT(std::string& s);

        inline size_t write(const char* s) { return write((uint8_t*)s, ::strlen(s)); }
        inline size_t write(unsigned long n) { return write((uint8_t)n); }
        inline size_t write(long n) { return write((uint8_t)n); }
        inline size_t write(unsigned int n) { return write((uint8_t)n); }
        inline size_t write(int n) { return write((uint8_t)n); }

        void flush(void) override {}

        uint32_t id() { return _clientNum; }

        int rx_buffer_available() override { return std::max(0, 256 - int(_queue.size())); }

        operator bool() const;

        ~WSChannel();

        int read() override;
        int available() override { return _queue.size() + (_rtchar > -1); }

        void autoReport() override;
        void active(bool is_active);

    private:
        AsyncWebSocket* _server;
        uint32_t           _clientNum;

        std::string _output_line;

        // Instead of queueing realtime characters, we put them here
        // so they can be processed immediately during operations like
        // homing where GCode handling is blocked.
        int _rtchar = -1;
    };

    class WSChannels {
    private:
        static std::map<uint32_t, WSChannel*> _wsChannels;
        static std::list<WSChannel*>         _webWsChannels;

        static WSChannel* _lastWSChannel;
        static WSChannel* getWSChannel(int pageid);

    public:
        static void removeChannel(WSChannel* channel);
        static void removeChannel(uint32_t num);

        static bool runGCode(int pageid, std::string_view cmd);
        static bool sendError(int pageid, std::string error);
        static void sendPing();
        static void handleEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
        static void handlev3Event(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
    };
}
