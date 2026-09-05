// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Channel.h"
#include "Serial.h"  // For allChannels
#include "lineedit.h"
#include <Arduino.h>

// Simple console channel that wraps Arduino's Serial (USB CDC)
class SerialChannel : public Channel {
private:
    Lineedit*       _lineedit;
    HardwareSerial* _serial;

public:
    SerialChannel(bool addCR = false) : Channel("SerialChannel", true), _serial(&Serial) {
        _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
    }

    void init() override {
        // Serial is already initialized in pico_main.cpp
        // Just register this channel with the system
        allChannels.registration(this);
    }

    // Stream write methods - output to Serial
    size_t write(uint8_t data) override { return _serial->write(data); }

    size_t write(const uint8_t* buffer, size_t length) override {
        // Replace \n with \r\n
        if (_addCR) {
            size_t rem      = length;
            char   lastchar = '\0';
            size_t j        = 0;
            while (rem) {
                const int bufsize = 80;
                uint8_t   modbuf[bufsize];
                // bufsize-1 in case the last character is \n
                size_t k = 0;
                while (rem && k < (bufsize - 1)) {
                    char c = buffer[j++];
                    if (c == '\n' && lastchar != '\r') {
                        modbuf[k++] = '\r';
                    }
                    lastchar    = c;
                    modbuf[k++] = c;
                    --rem;
                }
                size_t actual = _serial->write(modbuf, k);
                if (actual != k) {
                    // ::printf("dropped %d\n", k - actual);
                }
            }
            return length;
        }
        size_t actual = _serial->write(buffer, length);
        if (actual != length) {
            // ::printf("dropped %d\n", length - actual);
        }
        return actual;
    }

    // Stream read methods - input from Serial
    int read() override { return _serial->read(); }

    bool realtimeOkay(char c) { return _lineedit->realtime(c); }

    bool lineComplete(char* line, char c) {
        if (_lineedit->step(c)) {
            _linelen        = _lineedit->finish();
            _line[_linelen] = '\0';
            strcpy(line, _line);
            _linelen = 0;
            return true;
        }
        return false;
    }

    Error pollLine(char* line) {
        if (_lineedit == nullptr) {
            return Error::NoData;
        }
        return Channel::pollLine(line);
    }

    int available() override { return _serial->available(); }

    int peek() override { return _serial->peek(); }

    // Channel-specific methods
    void flushRx() override {
        // Arduino Serial doesn't have a flush method, but we can clear the buffer
        while (_serial->available()) {
            _serial->read();
        }
    }

    int rx_buffer_available() override {
        // Simple estimate - Arduino Serial doesn't expose buffer size directly
        return 128 - available();
    }
};

// Global console instance
SerialChannel serialConsole;
Channel&      Console = serialConsole;

