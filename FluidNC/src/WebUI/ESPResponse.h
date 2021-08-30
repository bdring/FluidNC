// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*

#include <cstdint>
#include <WString.h>
#include "../Serial.h"  // client_t

#ifdef ENABLE_WIFI
class WebServer;
#endif

namespace WebUI {
    class ESPResponseStream {
    public:
#ifdef ENABLE_WIFI
        ESPResponseStream(WebServer* webserver);
#endif

        ESPResponseStream(client_t client, bool byid = true);
        ESPResponseStream();

        void     print(const char data);
        void     print(const char* data);
        void     println(const char* data);
        void     flush();
        bool     anyOutput() { return _header_sent; }
        client_t client() { return _client; }

    private:
        client_t _client;
        bool     _header_sent;

#ifdef ENABLE_WIFI
        WebServer* _webserver;
        String     _buffer;
#endif
    };
}
