// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

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

        std::string toString() override { return _implementation->toString(); }

        ~DebugPinDetail() override {}
    };

}
