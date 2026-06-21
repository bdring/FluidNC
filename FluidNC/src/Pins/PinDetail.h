// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"
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
    public:
        std::string _name;
        pinnum_t    _index    = INVALID_PINNUM;
        bool        _inverted = false;

        PinDetail(pinnum_t index) : _index(index) {}
        PinDetail(const PinDetail& o)            = delete;
        PinDetail(PinDetail&& o)                 = delete;
        PinDetail& operator=(const PinDetail& o) = delete;
        PinDetail& operator=(PinDetail&& o)      = delete;

        virtual PinCapabilities capabilities() const = 0;

        // I/O:
        virtual void          write(bool high) = 0;
        virtual void          synchronousWrite(bool high);
        virtual void          setDuty(uint32_t duty) {};
        virtual uint32_t      maxDuty() { return 0; }
        virtual bool          read()                                               = 0;
        virtual void          setAttr(PinAttributes value, uint32_t frequency = 0) = 0;
        virtual PinAttributes getAttr() const                                      = 0;

        virtual int8_t driveStrength() { return -1; }

        virtual bool canStep() { return false; }

        virtual void registerEvent(InputPin* obj);

        const char* name() { return _name.c_str(); }

        inline pinnum_t number() const { return _index; }

        virtual ~PinDetail() {}
    };
}
