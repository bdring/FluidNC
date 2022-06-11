// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#ifdef ENABLE_WIFI
#    include "../Report.h"
#    include "WebClient.h"
#    include <WebServer.h>

namespace WebUI {
    WebClient::WebClient(WebServer* webserver, bool silent) :
        Channel("webclient"), _header_sent(false), _silent(silent), _webserver(webserver), _buflen(0) {}

    size_t WebClient::write(const uint8_t* buffer, size_t length) {
        if (_silent) {
            return length;
        }
        if (!_header_sent) {
            _webserver->setContentLength(CONTENT_LENGTH_UNKNOWN);
            _webserver->sendHeader("Content-Type", "text/html");
            _webserver->sendHeader("Cache-Control", "no-cache");
            _webserver->send(200);
            _header_sent = true;
        }

        size_t index = 0;
        while (index < length) {
            size_t copylen = std::min(length - index, BUFLEN - _buflen);
            memcpy(_buffer + _buflen, buffer + index, copylen);
            _buflen += copylen;
            index += copylen;
            if (_buflen >= BUFLEN) {  // The > case should not happen
                flush();
            }
        }

        return length;
    }

    size_t WebClient::write(uint8_t data) { return write(&data, 1); }

    void WebClient::flush() {
        if (_buflen) {
            _webserver->sendContent(_buffer, _buflen);
            _buflen = 0;
        }
    }
    void WebClient::flushRx() { Channel::flushRx(); }

    WebClient::~WebClient() {
        flush();
        _webserver->sendContent("");  //close connection
    }
}
#endif
