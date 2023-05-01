// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "PinDetail.h"
#include "PinOptionsParser.h"

namespace Pins {
    class VoidPinDetail : public PinDetail {
    public:
        VoidPinDetail(int number = 0);
        VoidPinDetail(const PinOptionsParser& options);

        PinCapabilities capabilities() const override;

        // I/O:
        void          write(int high) override;
        int           read() override;
        void          setAttr(PinAttributes value) override;
        PinAttributes getAttr() const override;

        std::string toString() override;

        ~VoidPinDetail() override {}
    };
}
