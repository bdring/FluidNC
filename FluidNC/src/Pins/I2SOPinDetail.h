// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

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

        bool _lastWrittenValue = false;

    public:
        I2SOPinDetail(pinnum_t index, const PinOptionsParser& options);

        PinCapabilities capabilities() const override;

        // I/O:
        void          write(int high) override;
        void          synchronousWrite(int high) override;
        int           read() override;
        void          setAttr(PinAttributes value) override;
        PinAttributes getAttr() const override;

        std::string toString() override;

        ~I2SOPinDetail() override { _claimed[_index] = false; }
    };
}

#endif
