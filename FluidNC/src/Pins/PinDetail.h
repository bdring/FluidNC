// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "PinAttributes.h"
#include "PinOptionsParser.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class InputPin;

namespace Pins {

    // Implementation details of pins.
    class PinDetail {
    protected:
    public:
        int  _index    = -1;
        bool _inverted = false;

        PinDetail(int number) : _index(number) {}
        PinDetail(const PinDetail& o)            = delete;
        PinDetail(PinDetail&& o)                 = delete;
        PinDetail& operator=(const PinDetail& o) = delete;
        PinDetail& operator=(PinDetail&& o)      = delete;

        virtual PinCapabilities capabilities() const = 0;

        // I/O:
        virtual void          write(int high) = 0;
        virtual void          synchronousWrite(int high);
        virtual void          setDuty(uint32_t duty) {};
        virtual uint32_t      maxDuty() { return 0; }
        virtual int           read()                                                = 0;
        virtual void          setAttr(PinAttributes value, uint32_t frequencey = 0) = 0;
        virtual PinAttributes getAttr() const                                       = 0;

        virtual int8_t driveStrength() { return -1; }

        virtual bool canStep() { return false; }

        virtual void registerEvent(InputPin* obj);

        virtual std::string toString() = 0;

        inline int number() const { return _index; }

        virtual ~PinDetail() {}
    };
}
