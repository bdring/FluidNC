// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstdint>
#include <cstring>
#include <list>
#include <map>

class WebSocketsServer;

#include "Channel.h"

namespace WebUI {
    class WSChannel : public Channel {
    public:
        WSChannel(WebSocketsServer* server, objnum_t clientNum);

        size_t write(uint8_t c);
        size_t write(const uint8_t* buffer, size_t size);

        bool sendTXT(std::string& s);

        inline size_t write(const char* s) { return write((uint8_t*)s, ::strlen(s)); }

        void flush(void) override {}

        objnum_t id() { return _clientNum; }

        int rx_buffer_available() override { return std::max(0, 256 - int(_queue.size())); }

        operator bool() const;

        ~WSChannel();

        int read() override;
        int available() override { return _queue.size() + (_rtchar > -1); }

        void autoReport() override;

    private:
        WebSocketsServer* _server;
        objnum_t          _clientNum;

        std::string _output_line;

        // Instead of queueing realtime characters, we put them here
        // so they can be processed immediately during operations like
        // homing where GCode handling is blocked.
        int32_t _rtchar = -1;
    };

    class WSChannels {
    private:
        static std::map<objnum_t, WSChannel*> _wsChannels;
        static std::list<WSChannel*>          _webWsChannels;

        static WSChannel* _lastWSChannel;
        static WSChannel* getWSChannel(uint32_t pageid);

    public:
        static void removeChannel(WSChannel* channel);
        static void removeChannel(objnum_t num);

        static bool runGCode(uint32_t pageid, std::string_view cmd);
        static bool sendError(uint32_t pageid, std::string error);
        static void sendPing();
        static void handleEvent(WebSocketsServer* server, objnum_t num, uint8_t type, uint8_t* payload, size_t length);
        static void handlev3Event(WebSocketsServer* server, objnum_t num, uint8_t type, uint8_t* payload, size_t length);
    };
}
