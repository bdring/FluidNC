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

        using ISRCallback        = void (*)(void*, int32_t);
        ISRCallback _isrCallback = nullptr;
        void*       _isrArgument = nullptr;

        static void IRAM_ATTR ISRCallbackHandler(void* arg) {
            auto gpio = static_cast<GPIOPinDetail*>(arg);
            if (gpio) {
                auto c = gpio->_isrCallback;  // store in local to avoid thread issues
                if (c) {
                    (*c)(gpio->_isrArgument, 0);
                }
            }
        }

    public:
        static const int nGPIOPins = 40;

        GPIOPinDetail(pinnum_t index, PinOptionsParser options);

        PinCapabilities capabilities() const override;

        // I/O:
        void write(int high) override;
        int IRAM_ATTR read() override;
        void          setAttr(PinAttributes value) override;
        PinAttributes getAttr() const override;

        // ISR's:
        void attachInterrupt(void (*callback)(void*, int32_t), void* arg) override;
        void detachInterrupt() override;

        String toString() override;

        ~GPIOPinDetail() override { _claimed[_index] = false; }
    };

}
