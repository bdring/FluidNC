// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ESPResponse.h"

#include "../Report.h"

#ifdef ENABLE_WIFI
#    include "WebServer.h"
#    include <WebServer.h>
#endif

namespace WebUI {
#ifdef ENABLE_WIFI
    ESPResponseStream::ESPResponseStream(WebServer* webserver) {
        _header_sent = false;
        _webserver   = webserver;
        _client      = CLIENT_WEBUI;
    }
#endif

    ESPResponseStream::ESPResponseStream() {
        _client = CLIENT_INPUT;
#ifdef ENABLE_WIFI
        _header_sent = false;
        _webserver   = NULL;
#endif
    }

    ESPResponseStream::ESPResponseStream(uint8_t client, bool byid) {
        (void)byid;  //fake parameter to avoid confusion with pointer one (NULL == 0)
        _client = client;
#ifdef ENABLE_WIFI
        _header_sent = false;
        _webserver   = NULL;
#endif
    }

    void ESPResponseStream::println(const char* data) {
        print(data);
        if (_client == CLIENT_TELNET) {
            print("\r\n");
        } else {
            print("\n");
        }
    }

    void ESPResponseStream::print(const char* data) {
        if (_client == CLIENT_INPUT) {
            return;
        }
#ifdef ENABLE_WIFI
        if (_webserver) {
            if (!_header_sent) {
                _webserver->setContentLength(CONTENT_LENGTH_UNKNOWN);
                _webserver->sendHeader("Content-Type", "text/html");
                _webserver->sendHeader("Cache-Control", "no-cache");
                _webserver->send(200);
                _header_sent = true;
            }

            _buffer += data;
            if (_buffer.length() > 1200) {
                //send data
                _webserver->sendContent(_buffer);
                //reset buffer
                _buffer = "";
            }
            return;
        }
#endif
        _send(_client, data);
    }

    void ESPResponseStream::print(const char data) {
        char text[2];
        text[0] = data;
        text[1] = '\0';
        print(text);
    }

    void ESPResponseStream::flush() {
#ifdef ENABLE_WIFI
        if (_webserver) {
            if (_header_sent) {
                //send data
                if (_buffer.length() > 0) {
                    _webserver->sendContent(_buffer);
                }

                //close connection
                _webserver->sendContent("");
            }
            _header_sent = false;
            _buffer      = "";
        }
#endif
    }
}
