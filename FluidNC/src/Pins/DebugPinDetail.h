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
    class DebugPinDetail : public PinDetail {
        PinDetail* _implementation;

        uint32_t _lastEvent;
        int      _eventCount;
        bool     _isHigh;

        struct CallbackHandler {
            void (*callback)(void* arg);
            void*           argument;
            DebugPinDetail* _myPin;

            static void handle(void* arg);
        } _isrHandler;

        friend void CallbackHandler::handle(void* arg);

        bool shouldEvent();

    public:
        DebugPinDetail(PinDetail* implementation) :
            PinDetail(implementation->number()), _implementation(implementation), _lastEvent(0), _eventCount(0), _isHigh(false),
            _isrHandler({ 0 }) {}

        PinCapabilities capabilities() const override { return _implementation->capabilities(); }

        // I/O:
        void          write(int high) override;
        int           read() override;
        void          setAttr(PinAttributes value) override;
        PinAttributes getAttr() const override;

        // ISR's:
        void attachInterrupt(void (*callback)(void*), void* arg, int mode) override;
        void detachInterrupt() override;

        String toString() override { return _implementation->toString(); }

        ~DebugPinDetail() override {}
    };

}
