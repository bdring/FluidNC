// Copyright (c) 2025 - FluidNC contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "PinDetail.h"

namespace Pins {
    class SimPinDetail : public PinDetail {
        pinnum_t      _axis;
        PinAttributes _attributes = PinAttributes::Output;

    public:
        SimPinDetail(pinnum_t axis, PinOptionsParser options);

        PinCapabilities capabilities() const override;

        // I/O:
        void          write(bool high) override;
        bool          read() override;
        void          setAttr(PinAttributes value, uint32_t frequency = 0) override;
        PinAttributes getAttr() const override;

        bool canStep() override { return true; }

        ~SimPinDetail() override = default;
    };
}
