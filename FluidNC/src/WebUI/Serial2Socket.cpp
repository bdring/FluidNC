// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Serial2Socket.h"

namespace WebUI {
    Serial_2_Socket serial2Socket;
}

#ifdef ENABLE_WIFI
#    include "WebServer.h"
#    include <WebSocketsServer.h>
#    include <WiFi.h>

namespace WebUI {
    Serial_2_Socket::Serial_2_Socket() : Channel("websocket"), _web_socket(nullptr), _TXbufferSize(0), _RXbufferSize(0), _RXbufferpos(0) {}

    void Serial_2_Socket::begin(long speed) { _TXbufferSize = 0; }

    void Serial_2_Socket::end() { _TXbufferSize = 0; }

    int Serial_2_Socket::read() {
        if (_rtchar == -1) {
            return -1;
        } else {
            auto ret = _rtchar;
            _rtchar  = -1;
            return ret;
        }
    }

    bool Serial_2_Socket::attachWS(WebSocketsServer* web_socket) {
        if (web_socket) {
            _web_socket   = web_socket;
            _TXbufferSize = 0;
            return true;
        }
        return false;
    }

    bool Serial_2_Socket::detachWS() {
        _web_socket = NULL;
        return true;
    }

    Serial_2_Socket::operator bool() const { return true; }

    size_t Serial_2_Socket::write(uint8_t c) {
        if (!_web_socket) {
            return 0;
        }
        write(&c, 1);
        return 1;
    }

    size_t Serial_2_Socket::write(const uint8_t* buffer, size_t size) {
        if (buffer == NULL) {
            log_i("[SOCKET]No buffer");
            return 0;
        }
        if (!_web_socket) {
            log_i("[SOCKET]No socket");
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
        log_i("[SOCKET]buffer size %d", _TXbufferSize);
        handle();
        return size;
    }

    void Serial_2_Socket::pushRT(char ch) { _rtchar = ch; }

    bool Serial_2_Socket::push(const uint8_t* data, size_t length) {
        while (length--) {
            uint8_t c = *data++;
            // Skip UTF-8 encoding prefix C2 and spurious nulls
            // A null in this case is not end-of-string but rather
            // an artifact of a bug in WebUI that improperly converts
            // realtime characters to strings
            if (c != 0xc2 && c != 0) {
                if (is_realtime_command(c)) {
                    _rtchar = c;
                } else {
                    _queue.push(char(c));
                }
            }
        }
        return true;
    }
    bool Serial_2_Socket::push(const String& s) { return push((uint8_t*)s.c_str(), s.length()); }

    void Serial_2_Socket::handle() {
        if (_TXbufferSize > 0 && ((_TXbufferSize >= TXBUFFERSIZE) || ((millis() - _lastflush) > FLUSHTIMEOUT))) {
            log_i("[SOCKET]need flush, buffer size %d", _TXbufferSize);
            flush();
        }
    }
    void Serial_2_Socket::flush(void) {
        if (_TXbufferSize > 0) {
            log_i("[SOCKET]flush data, buffer size %d", _TXbufferSize);
            _web_socket->broadcastBIN(_TXbuffer, _TXbufferSize);

            //refresh timout
            _lastflush = millis();

            //reset buffer
            _TXbufferSize = 0;
        }
    }

    Serial_2_Socket::~Serial_2_Socket() {
        if (_web_socket) {
            detachWS();
        }
        _TXbufferSize = 0;
    }
}
#endif
