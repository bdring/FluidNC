// Copyright (c) 2023 B. Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#ifdef ESP32

#    include "PinDetail.h"

namespace Pins {
    class UARTIODetail : public PinDetail {
        PinCapabilities _capabilities;
        PinAttributes   _attributes;
        int             _readWriteMask;

        int _device_id;

        static const int         nPins = 255;
        static std::vector<bool> _claimed;

        bool _lastWrittenValue = false;

    public:
        UARTIODetail(int deviceId, pinnum_t index, const PinOptionsParser& options);

        // I/O:
        PinCapabilities capabilities() const override;
        void          write(int high) override;
        int           read() override;
        void          setAttr(PinAttributes value) override;
        PinAttributes getAttr() const override;

        std::string toString() override;

        ~UARTIODetail() override { _claimed[_index] = false; }
    };
}

#endif
