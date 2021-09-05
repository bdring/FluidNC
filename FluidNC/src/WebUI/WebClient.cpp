// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "../Report.h"

#ifdef ENABLE_WIFI
#    include "WebClient.h"
#    include <WebServer.h>
#endif

namespace WebUI {
#ifdef ENABLE_WIFI
    WebClient::WebClient(WebServer* webserver, bool silent) : _header_sent(false), _silent(silent), _webserver(webserver), _buflen(0) {}
#endif

    size_t WebClient::write(const uint8_t* buffer, size_t length) {
        if (_silent) {
            return length;
        }
#ifdef ENABLE_WIFI
        if (!_header_sent) {
            _webserver->setContentLength(CONTENT_LENGTH_UNKNOWN);
            _webserver->sendHeader("Content-Type", "text/html");
            _webserver->sendHeader("Cache-Control", "no-cache");
            _webserver->send(200);
            _header_sent = true;
        }

        size_t remaining = length;
        while (remaining) {
            size_t copylen = std::min(remaining, BUFLEN - _buflen);
            memcpy(&_buffer[_buflen], buffer, copylen);
            _buflen += copylen;
            remaining -= copylen;
            if (_buflen >= BUFLEN) {  // The > case should not happen
                flush();
            }
        }
        return length;
#else
        return 0;
#endif
    }

    size_t WebClient::write(uint8_t data) { return write(&data, 1); }

    void WebClient::flush() {
#ifdef ENABLE_WIFI
        if (_buflen) {
            _webserver->sendContent(_buffer, _buflen);
            _buflen = 0;
        }
#endif
    }

    WebClient::~WebClient() {
        flush();
#ifdef ENABLE_WIFI
        _webserver->sendContent("");  //close connection
#endif
    }
}
