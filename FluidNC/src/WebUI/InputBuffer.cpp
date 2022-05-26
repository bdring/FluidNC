// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "../Config.h"
#include "InputBuffer.h"

namespace WebUI {
    InputBuffer inputBuffer;

    InputBuffer::InputBuffer() : Channel("macros"), _RXbufferSize(0), _RXbufferpos(0) {}

    void InputBuffer::begin() {
        _RXbufferSize = 0;
        _RXbufferpos  = 0;
    }

    void InputBuffer::flushRx() {
        begin();
        Channel::flushRx();
    }

    void InputBuffer::end() { begin(); }

    InputBuffer::operator bool() const { return true; }

    int InputBuffer::available() { return _RXbufferSize; }

    int InputBuffer::availableforwrite() { return 0; }

    int InputBuffer::peek(void) {
        if (_RXbufferSize > 0) {
            return _RXbuffer[_RXbufferpos];
        } else {
            return -1;
        }
    }
    bool InputBuffer::push(const char* data) {
        int data_size = strlen(data);
        if ((data_size + _RXbufferSize) <= RXBUFFERSIZE) {
            int current = _RXbufferpos + _RXbufferSize;
            if (current > RXBUFFERSIZE) {
                current = current - RXBUFFERSIZE;
            }
            for (int i = 0; i < data_size; i++) {
                if (current > (RXBUFFERSIZE - 1)) {
                    current = 0;
                }
                _RXbuffer[current] = data[i];
                current++;
            }
            _RXbufferSize += strlen(data);
            return true;
        }
        return false;
    }
    bool InputBuffer::push(char data) {
        char buf[2] = { data, '\0' };
        return push(buf);
    }

    int InputBuffer::read(void) {
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

    InputBuffer::~InputBuffer() { begin(); }
}
