// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "WSChannel.h"

#ifdef ENABLE_WIFI
#    include "WebServer.h"
#    include <WebSocketsServer.h>
#    include <WiFi.h>

namespace WebUI {
    WSChannel::WSChannel(WebSocketsServer* server, uint8_t clientNum) :
        Channel("websocket"), _server(server), _clientNum(clientNum), _TXbufferSize(0), _RXbufferSize(0), _RXbufferpos(0) {}

    int WSChannel::read() {
        if (_rtchar == -1) {
            return -1;
        } else {
            auto ret = _rtchar;
            _rtchar  = -1;
            return ret;
        }
    }

    WSChannel::operator bool() const { return true; }

    size_t WSChannel::write(uint8_t c) { return write(&c, 1); }

    size_t WSChannel::write(const uint8_t* buffer, size_t size) {
        if (buffer == NULL) {
            return 0;
        }

        if (_TXbufferSize == 0) {
            _lastflush = millis();
        }

        for (int i = 0; i < size; i++) {
            if (_TXbufferSize >= TXBUFFERSIZE) {
                flush();
            }
            _TXbuffer[_TXbufferSize] = buffer[i];
            _TXbufferSize++;
        }
        handle();
        return size;
    }

    void WSChannel::pushRT(char ch) { _rtchar = ch; }

    bool WSChannel::push(const uint8_t* data, size_t length) {
        char c;
        while ((c = *data++) != '\0') {
            _queue.push(c);
        }
        return true;
    }

    bool WSChannel::push(String& s) { return push((uint8_t*)s.c_str(), s.length()); }

    void WSChannel::handle() {
        if (_TXbufferSize > 0 && ((_TXbufferSize >= TXBUFFERSIZE) || ((millis() - _lastflush) > FLUSHTIMEOUT))) {
            flush();
        }
    }
    size_t WSChannel::sendTXT(String& s) { return _server->sendTXT(_clientNum, s); }
    void   WSChannel::flush(void) {
        if (_TXbufferSize > 0) {
            _server->sendBIN(_clientNum, _TXbuffer, _TXbufferSize);

            //refresh timout
            _lastflush = millis();

            //reset buffer
            _TXbufferSize = 0;
        }
    }

    WSChannel::~WSChannel() {}
}
#endif
