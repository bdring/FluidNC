// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "PinDetail.h"

namespace Pins {
    class GPIOPinDetail : public PinDetail {
        PinCapabilities _capabilities;
        PinAttributes   _attributes;
        int             _readWriteMask;

        static PinCapabilities GetDefaultCapabilities(pinnum_t index);

        static std::vector<bool> _claimed;

        bool _lastWrittenValue = false;

        static void gpioAction(int, void*, bool);

    public:
        static const int nGPIOPins = 40;

        GPIOPinDetail(pinnum_t index, PinOptionsParser options);

        PinCapabilities capabilities() const override;

        // I/O:
        void          write(int high) override;
        int IRAM_ATTR read() override;
        void          setAttr(PinAttributes value) override;
        PinAttributes getAttr() const override;

        void registerEvent(EventPin* obj) override;

        std::string toString() override;

        ~GPIOPinDetail() override { _claimed[_index] = false; }
    };

}
