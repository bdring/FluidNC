// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*

#include <cstdint>
#include <cstring>
#include <list>
#include <map>

class WebSocketsServer;

#ifndef ENABLE_WIFI
#    if 0
namespace WebUI {
    class WSChannel {
    public:
        WSChannel(WebSocketsServer* server, uint8_t clientNum);
        int    read() { return -1; }
        size_t write(const uint8_t* buffer, size_t size) { return 0; }
    };
}
#    endif
#else

#    include "../Channel.h"

namespace WebUI {
    class WSChannel : public Channel {
    public:
        WSChannel(WebSocketsServer* server, uint8_t clientNum);

        size_t write(uint8_t c);
        size_t write(const uint8_t* buffer, size_t size);

        bool sendTXT(std::string& s);

        inline size_t write(const char* s) { return write((uint8_t*)s, ::strlen(s)); }
        inline size_t write(unsigned long n) { return write((uint8_t)n); }
        inline size_t write(long n) { return write((uint8_t)n); }
        inline size_t write(unsigned int n) { return write((uint8_t)n); }
        inline size_t write(int n) { return write((uint8_t)n); }

        void flush(void) override {}

        int id() { return _clientNum; }

        int rx_buffer_available() override { return std::max(0, 256 - int(_queue.size())); }

        operator bool() const;

        ~WSChannel();

        int read() override;
        int available() override { return _queue.size() + (_rtchar > -1); }

        void autoReport() override;

    private:
        WebSocketsServer* _server;
        uint8_t           _clientNum;

        std::string _output_line;

        // Instead of queueing realtime characters, we put them here
        // so they can be processed immediately during operations like
        // homing where GCode handling is blocked.
        int _rtchar = -1;
    };

    class WSChannels {
    private:
        static std::map<uint8_t, WSChannel*> _wsChannels;
        static std::list<WSChannel*>         _webWsChannels;

        static WSChannel* _lastWSChannel;
        static WSChannel* getWSChannel(int pageid);

    public:
        static void removeChannel(WSChannel* channel);
        static void removeChannel(uint8_t num);

        static bool runGCode(int pageid, std::string_view cmd);
        static bool sendError(int pageid, std::string error);
        static void sendPing();
        static void handleEvent(WebSocketsServer* server, uint8_t num, uint8_t type, uint8_t* payload, size_t length);
        static void handlev3Event(WebSocketsServer* server, uint8_t num, uint8_t type, uint8_t* payload, size_t length);
    };
}

#endif
