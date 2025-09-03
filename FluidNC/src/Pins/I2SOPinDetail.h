// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"
#if MAX_N_I2SO
#    include "PinDetail.h"

namespace Pins {
    class I2SOPinDetail : public PinDetail {
        PinCapabilities _capabilities;
        PinAttributes   _attributes;

        static const int32_t         nI2SOPins = 32;
        static std::vector<bool> _claimed;

        bool _lastWrittenValue = false;

    public:
        I2SOPinDetail(pinnum_t index, const PinOptionsParser& options);

        PinCapabilities capabilities() const override;

        // I/O:
        void          write(int32_t high) override;
        void          synchronousWrite(int32_t high) override;
        int32_t           read() override;
        void          setAttr(PinAttributes value, uint32_t frequency) override;
        PinAttributes getAttr() const override;

        bool canStep() override { return true; }

        std::string toString() override;

        ~I2SOPinDetail() override { _claimed[_index] = false; }
    };
}

#endif
