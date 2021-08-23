/*
    Part of Grbl_ESP32
    2021 -  Stefan de Bruijn

    Grbl_ESP32 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Grbl_ESP32 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Grbl_ESP32.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "PinCapabilities.h"
#include "PinAttributes.h"
#include "PinOptionsParser.h"

#include <WString.h>
#include <cstdint>
#include <cstring>
#include <vector>

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
        virtual void          synchronousWrite(int high) { write(high); }
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
