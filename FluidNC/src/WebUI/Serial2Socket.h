// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*

#include <cstdint>
#include <cstring>

#ifndef ENABLE_WIFI
namespace WebUI {
    class Serial_2_Socket {
    public:
        Serial_2_Socket() = default;
        int    read() { return -1; }
        void   handle_flush() {}
        size_t write(const uint8_t* buffer, size_t size) { return 0; }
    };
    extern Serial_2_Socket Serial2Socket;
}
#else

#    include "../Channel.h"

class WebSocketsServer;

namespace WebUI {
    class Serial_2_Socket : public Channel {
        static const int TXBUFFERSIZE = 1200;
        static const int RXBUFFERSIZE = 256;
        static const int FLUSHTIMEOUT = 500;

    public:
        Serial_2_Socket();

        size_t write(uint8_t c);
        size_t write(const uint8_t* buffer, size_t size);

        inline size_t write(const char* s) { return write((uint8_t*)s, ::strlen(s)); }
        inline size_t write(unsigned long n) { return write((uint8_t)n); }
        inline size_t write(long n) { return write((uint8_t)n); }
        inline size_t write(unsigned int n) { return write((uint8_t)n); }
        inline size_t write(int n) { return write((uint8_t)n); }

        long baudRate();
        void begin(long speed);
        void end();
        int  available();
        int  peek(void);
        int  read(void);
        bool push(const uint8_t* data, size_t length);
        bool push(const char* data);
        void flush(void);
        void handle_flush();
        bool attachWS(WebSocketsServer* web_socket);
        bool detachWS();

        int rx_buffer_available() { return RXBUFFERSIZE - available(); }

        operator bool() const;

        ~Serial_2_Socket();

    private:
        uint32_t          _lastflush;
        WebSocketsServer* _web_socket;

        uint8_t  _TXbuffer[TXBUFFERSIZE];
        uint16_t _TXbufferSize;

        uint8_t  _RXbuffer[RXBUFFERSIZE];
        uint16_t _RXbufferSize;
        uint16_t _RXbufferpos;
    };

    extern Serial_2_Socket Serial2Socket;
}

#endif
