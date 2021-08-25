// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <Print.h>
#include <cstring>

namespace WebUI {
    class InputBuffer : public Print {
    public:
        InputBuffer();

        size_t        write(uint8_t c);
        size_t        write(const uint8_t* buffer, size_t size);
        inline size_t write(const char* s) { return write((uint8_t*)s, ::strlen(s)); }
        inline size_t write(unsigned long n) { return write((uint8_t)n); }
        inline size_t write(long n) { return write((uint8_t)n); }
        inline size_t write(unsigned int n) { return write((uint8_t)n); }
        inline size_t write(int n) { return write((uint8_t)n); }
        void          begin();
        void          end();
        int           available();
        int           availableforwrite();
        int           peek(void);
        int           read(void);
        bool          push(const char* data);
        void          flush(void);

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
