// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*

#include <cstdint>
#include <cstring>

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
        static const int TXBUFFERSIZE = 1200;
        static const int RXBUFFERSIZE = 256;
        static const int FLUSHTIMEOUT = 500;

    public:
        WSChannel(WebSocketsServer* server, uint8_t clientNum);

        size_t write(uint8_t c);
        size_t write(const uint8_t* buffer, size_t size);

        size_t sendTXT(String& s);

        inline size_t write(const char* s) { return write((uint8_t*)s, ::strlen(s)); }
        inline size_t write(unsigned long n) { return write((uint8_t)n); }
        inline size_t write(long n) { return write((uint8_t)n); }
        inline size_t write(unsigned int n) { return write((uint8_t)n); }
        inline size_t write(int n) { return write((uint8_t)n); }

        //        void begin(long speed);
        //        void end();
        void handle();

        bool push(const uint8_t* data, size_t length);
        bool push(String& s);
        void pushRT(char ch);

        void flush(void);

        int id() { return _clientNum; }

        int rx_buffer_available() override { return RXBUFFERSIZE - available(); }

        operator bool() const;

        ~WSChannel();

        int read() override;
        int available() override { return _rtchar == -1 ? 0 : 1; }

    private:
        uint32_t          _lastflush;
        WebSocketsServer* _server;
        uint8_t           _clientNum;

        uint8_t  _TXbuffer[TXBUFFERSIZE];
        uint16_t _TXbufferSize;

        uint8_t  _RXbuffer[RXBUFFERSIZE];
        uint16_t _RXbufferSize;
        uint16_t _RXbufferpos;

        // Instead of queueing realtime characters, we put them here
        // so they can be processed immediately during operations like
        // homing where GCode handling is blocked.
        int _rtchar = -1;
    };
}

#endif
