// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*
#include "../Channel.h"

#ifdef ENABLE_WIFI

#    include "../Settings.h"

class WiFiServer;
class WiFiClient;

namespace WebUI {
    class Telnet_Server : public Channel {
        static const int DEFAULT_TELNET_STATE      = 1;
        static const int DEFAULT_TELNETSERVER_PORT = 23;

        static const int MAX_TELNET_PORT = 65001;
        static const int MIN_TELNET_PORT = 1;

        //how many clients should be able to telnet to this ESP32
        static const int MAX_TLNT_CLIENTS = 1;

        static const int TELNETRXBUFFERSIZE = 1200;
        static const int FLUSHTIMEOUT       = 500;

    public:
        Telnet_Server();

        bool   begin();
        void   end();
        void   handle() override;
        size_t write(uint8_t data) override;
        size_t write(const uint8_t* buffer, size_t size);
        int    read(void);
        int    peek(void);
        int    available();
        bool   push(uint8_t data);
        bool   push(const uint8_t* data, int datasize);
        void   flush() override {}

        int rx_buffer_available() override;

        static uint16_t port() { return _port; }

        ~Telnet_Server();

    private:
        static bool        _setupdone;
        static WiFiServer* _telnetserver;
        static WiFiClient  _telnetClients[MAX_TLNT_CLIENTS];
        static IPAddress   _telnetClientsIP[MAX_TLNT_CLIENTS];
        static uint16_t    _port;

        void clearClients();

        uint32_t _lastflush;
        uint8_t  _RXbuffer[TELNETRXBUFFERSIZE];
        uint16_t _RXbufferSize;
        uint16_t _RXbufferpos;
    };

    extern Telnet_Server telnet_server;
}

#endif
