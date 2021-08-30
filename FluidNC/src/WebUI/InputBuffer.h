// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <Stream.h>
#include <cstring>

namespace WebUI {
    class InputBuffer : public Stream {
    public:
        InputBuffer();

        size_t write(uint8_t c) override { return 0; }
        void   begin();
        void   end();
        int    available();
        int    availableforwrite();
        int    peek(void);
        int    read(void);
        bool   push(const char* data);
        bool   push(char data);
        void   flush(void);

        operator bool() const;

        ~InputBuffer();

    private:
        static const int RXBUFFERSIZE = 256;

        uint8_t  _RXbuffer[RXBUFFERSIZE];
        uint16_t _RXbufferSize;
        uint16_t _RXbufferpos;
    };

    extern InputBuffer inputBuffer;
}
