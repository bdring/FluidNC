// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "PinCapabilities.h"
#include "PinAttributes.h"
#include "PinOptionsParser.h"

#include <WString.h>
#include <cstdint>
#include <cstring>
#include <vector>

typedef uint8_t pinnum_t;

namespace Pins {

    // Implementation details of pins.
    class PinDetail {
    protected:
    public:
        int _index;

        PinDetail(int number) : _index(number) {}
        PinDetail(const PinDetail& o) = delete;
        PinDetail(PinDetail&& o)      = delete;
        PinDetail& operator=(const PinDetail& o) = delete;
        PinDetail& operator=(PinDetail&& o) = delete;

        virtual PinCapabilities capabilities() const = 0;

        // I/O:
        virtual void          write(int high) = 0;
        virtual void          synchronousWrite(int high);
        virtual int           read()                       = 0;
        virtual void          setAttr(PinAttributes value) = 0;
        virtual PinAttributes getAttr() const              = 0;

        // ISR's.
        virtual void attachInterrupt(void (*callback)(void*), void* arg, int mode);
        virtual void detachInterrupt();

        virtual String toString() = 0;

        inline int number() const { return _index; }

        virtual ~PinDetail() {}
    };
}
