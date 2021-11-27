// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Serial2Socket.h"

namespace WebUI {
    Serial_2_Socket Serial2Socket;
}

#ifdef ENABLE_WIFI
#    include "WebServer.h"
#    include <WebSocketsServer.h>
#    include <WiFi.h>

namespace WebUI {
    Serial_2_Socket::Serial_2_Socket() : Channel("websocket"), _web_socket(nullptr), _TXbufferSize(0), _RXbufferSize(0), _RXbufferpos(0) {}

    void Serial_2_Socket::begin(long speed) {
        _TXbufferSize = 0;
        _RXbufferSize = 0;
        _RXbufferpos  = 0;
    }

    void Serial_2_Socket::end() {
        _TXbufferSize = 0;
        _RXbufferSize = 0;
        _RXbufferpos  = 0;
    }

    long Serial_2_Socket::baudRate() { return 0; }

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

    int Serial_2_Socket::available() { return _RXbufferSize; }

    size_t Serial_2_Socket::write(uint8_t c) {
        if (!_web_socket) {
            return 0;
        }
        write(&c, 1);
        return 1;
    }

    size_t Serial_2_Socket::write(const uint8_t* buffer, size_t size) {
        if ((buffer == NULL) || (!_web_socket)) {
            if (buffer == NULL) {
                log_i("[SOCKET]No buffer");
            }
            if (!_web_socket) {
                log_i("[SOCKET]No socket");
            }
            return 0;
        }

        if (_TXbufferSize == 0) {
            _lastflush = millis();
        }

        //send full line
        if (_TXbufferSize + size > TXBUFFERSIZE) {
            flush();
        }

        //need periodic check to force to flush in case of no end
        for (int i = 0; i < size; i++) {
            _TXbuffer[_TXbufferSize] = buffer[i];
            _TXbufferSize++;
        }
        log_i("[SOCKET]buffer size %d", _TXbufferSize);
        handle_flush();
        return size;
    }

    int Serial_2_Socket::peek(void) {
        if (_RXbufferSize > 0) {
            return _RXbuffer[_RXbufferpos];
        } else {
            return -1;
        }
    }

    bool Serial_2_Socket::push(const uint8_t* data, size_t length) {
        if ((length + _RXbufferSize) <= RXBUFFERSIZE) {
            int current = _RXbufferpos + _RXbufferSize;
            if (current > RXBUFFERSIZE) {
                current = current - RXBUFFERSIZE;
            }

            for (int i = 0; i < length; i++) {
                if (current > (RXBUFFERSIZE - 1)) {
                    current = 0;
                }
                _RXbuffer[current] = data[i];
                current++;
            }

            _RXbufferSize += length;
            return true;
        }
        return false;
    }

    bool Serial_2_Socket::push(const char* data) { return push((uint8_t*)data, strlen(data)); }

    int Serial_2_Socket::read(void) {
        handle_flush();
        if (_RXbufferSize > 0) {
            int v = _RXbuffer[_RXbufferpos];
            _RXbufferpos++;

            if (_RXbufferpos > (RXBUFFERSIZE - 1)) {
                _RXbufferpos = 0;
            }
            _RXbufferSize--;
            return v;
        } else {
            return -1;
        }
    }

    void Serial_2_Socket::handle_flush() {
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
        _RXbufferSize = 0;
        _RXbufferpos  = 0;
    }
}
#endif
