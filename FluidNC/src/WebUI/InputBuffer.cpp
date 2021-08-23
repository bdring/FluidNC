/*
  InputBuffer.cpp -  inputbuffer functions class

  Copyright (c) 2014 Luc Lebosse. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "../Config.h"
#include "InputBuffer.h"

namespace WebUI {
    InputBuffer inputBuffer;

    InputBuffer::InputBuffer() {
        _RXbufferSize = 0;
        _RXbufferpos  = 0;
    }

    void InputBuffer::begin() {
        _RXbufferSize = 0;
        _RXbufferpos  = 0;
    }

    void InputBuffer::end() {
        _RXbufferSize = 0;
        _RXbufferpos  = 0;
    }

    InputBuffer::operator bool() const { return true; }

    int InputBuffer::available() { return _RXbufferSize; }

    int InputBuffer::availableforwrite() { return RXBUFFERSIZE - _RXbufferSize; }

    size_t InputBuffer::write(uint8_t c) {
        if ((1 + _RXbufferSize) <= RXBUFFERSIZE) {
            int current = _RXbufferpos + _RXbufferSize;
            if (current > RXBUFFERSIZE) {
                current = current - RXBUFFERSIZE;
            }
            if (current > (RXBUFFERSIZE - 1)) {
                current = 0;
            }
            _RXbuffer[current] = c;
            current++;
            _RXbufferSize += 1;
            return 1;
        }
        return 0;
    }

    size_t InputBuffer::write(const uint8_t* buffer, size_t size) {
        //No need currently
        //keep for compatibility
        return size;
    }

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

    void InputBuffer::flush(void) {
        //No need currently
        //keep for compatibility
    }

    InputBuffer::~InputBuffer() {
        _RXbufferSize = 0;
        _RXbufferpos  = 0;
    }
}
