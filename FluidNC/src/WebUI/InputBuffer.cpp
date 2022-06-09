// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "../Config.h"
#include "InputBuffer.h"

namespace WebUI {
    InputBuffer inputBuffer;

    InputBuffer::InputBuffer() : Channel("macros") {}

    InputBuffer::operator bool() const { return true; }

    bool InputBuffer::push(const char* data) {
        char c;
        while ((c = *data++) != '\0') {
            _queue.push(c);
        }
        return true;
    }
    bool InputBuffer::push(char data) {
        char buf[2] = { data, '\0' };
        return push(buf);
    }

    InputBuffer::~InputBuffer() {}
}
