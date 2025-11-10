// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "PinDetail.h"
#include "PinOptionsParser.h"
#include <string_view>

namespace Pins {
    class ErrorPinDetail : public PinDetail {
        std::string _description;

    public:
        explicit ErrorPinDetail(std::string_view descr);

        PinCapabilities capabilities() const override;

        // I/O will all give an error:
        void          write(bool high) override;
        bool          read() override;
        void          setAttr(PinAttributes value, uint32_t frequency) override;
        PinAttributes getAttr() const override;

        ~ErrorPinDetail() override {}
    };

}
