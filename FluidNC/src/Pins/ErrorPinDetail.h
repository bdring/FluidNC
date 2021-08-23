// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "PinDetail.h"
#include "PinOptionsParser.h"

namespace Pins {
    class ErrorPinDetail : public PinDetail {
        String _description;

    public:
        ErrorPinDetail(const String& descr);

        PinCapabilities capabilities() const override;

        // I/O will all give an error:
        void          write(int high) override;
        int           read() override;
        void          setAttr(PinAttributes value) override;
        PinAttributes getAttr() const override;

        String toString() override;

        ~ErrorPinDetail() override {}
    };

}
