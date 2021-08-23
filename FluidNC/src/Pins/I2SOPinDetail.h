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
#ifdef ESP32

#    include "PinDetail.h"

namespace Pins {
    class I2SOPinDetail : public PinDetail {
        PinCapabilities _capabilities;
        PinAttributes   _attributes;
        int             _readWriteMask;

        static const int         nI2SOPins = 32;
        static std::vector<bool> _claimed;

    public:
        I2SOPinDetail(uint8_t index, const PinOptionsParser& options);

        PinCapabilities capabilities() const override;

        // I/O:
        void          write(int high) override;
        void          synchronousWrite(int high) override;
        int           read() override;
        void          setAttr(PinAttributes value) override;
        PinAttributes getAttr() const override;

        String toString() override;

        ~I2SOPinDetail() override { _claimed[_index] = false; }
    };
}

#endif
