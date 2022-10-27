// Copyright (c) 2021 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*
#include "../Channel.h"

#ifdef ENABLE_WIFI
class WebServer;

namespace WebUI {
    class WebClient : public Channel {
    public:
        WebClient();
        ~WebClient();

        void attachWS(WebServer* webserver, bool silent);
        void detachWS();

        size_t write(uint8_t data) override;
        size_t write(const uint8_t* buffer, size_t length) override;
        void   flush();

        bool anyOutput() { return _header_sent; }

    private:
        bool                _header_sent = false;
        bool                _silent      = false;
        WebServer*          _webserver   = nullptr;
        static const size_t BUFLEN       = 1200;
        char                _buffer[BUFLEN];
        size_t              _buflen = 0;
    };

    extern WebClient webClient;
}

#endif
