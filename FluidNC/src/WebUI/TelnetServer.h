#pragma once

/*
  TelnetServer.h -  telnet service functions class

  Copyright (c) 2014 Luc Lebosse. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "../Config.h"  // ENABLE_*

#ifndef ENABLE_WIFI
namespace WebUI {
    class Telnet_Server {
    public:
        Telnet_Server() = default;
        int    read() { return -1; }
        size_t write(const uint8_t* buffer, size_t size) { return 0; }
        size_t get_rx_buffer_available() { return 0; }
    };
    extern Telnet_Server telnet_server;
}
#else

class WiFiServer;
class WiFiClient;

namespace WebUI {
    class Telnet_Server {
        //how many clients should be able to telnet to this ESP32
        static const int MAX_TLNT_CLIENTS = 1;

        static const int TELNETRXBUFFERSIZE = 1200;
        static const int FLUSHTIMEOUT       = 500;

    public:
        Telnet_Server();

        bool   begin();
        void   end();
        void   handle();
        size_t write(const uint8_t* buffer, size_t size);
        int    read(void);
        int    peek(void);
        int    available();
        int    get_rx_buffer_available();
        bool   push(uint8_t data);
        bool   push(const uint8_t* data, int datasize);

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
