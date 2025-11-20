// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "PinDetail.h"

namespace Pins {
    class DebugPinDetail : public PinDetail {
        PinDetail* _implementation;

        uint32_t _lastEvent;
        bool     _isHigh;

        struct CallbackHandler {
            void (*callback)(void* arg, bool v);
            void*           argument;
            DebugPinDetail* _myPin;

            static void handle(void* arg);
        } _isrHandler;

        friend void CallbackHandler::handle(void* arg);

        bool shouldEvent();

    public:
        explicit DebugPinDetail(PinDetail* implementation) :
            PinDetail(implementation->number()), _implementation(implementation), _lastEvent(0), _isHigh(false), _isrHandler({}) {}

        PinCapabilities capabilities() const override { return _implementation->capabilities(); }

        // I/O:
        void          write(bool high) override;
        bool          read() override;
        void          setAttr(PinAttributes value, uint32_t frequency) override;
        PinAttributes getAttr() const override;

        const char* name() { return _implementation->name(); }

        ~DebugPinDetail() override {}
    };

}
