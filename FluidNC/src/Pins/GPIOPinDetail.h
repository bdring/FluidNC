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

#include "PinDetail.h"

namespace Pins {
    class GPIOPinDetail : public PinDetail {
        PinCapabilities _capabilities;
        PinAttributes   _attributes;
        int             _readWriteMask;

        static PinCapabilities GetDefaultCapabilities(uint8_t index);

        static const int         nGPIOPins = 40;
        static std::vector<bool> _claimed;

    public:
        GPIOPinDetail(uint8_t index, PinOptionsParser options);

        PinCapabilities capabilities() const override;

        // I/O:
        void          write(int high) override;
        int IRAM_ATTR read() override;
        void          setAttr(PinAttributes value) override;
        PinAttributes getAttr() const override;

        // ISR's:
        void attachInterrupt(void (*callback)(void*), void* arg, int mode) override;
        void detachInterrupt() override;

        String toString() override;

        ~GPIOPinDetail() override { _claimed[_index] = false; }
    };

}
