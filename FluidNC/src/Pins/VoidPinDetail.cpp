// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <esp_attr.h>  // IRAM_ATTR
#include "VoidPinDetail.h"

namespace Pins {
    VoidPinDetail::VoidPinDetail(int number) : PinDetail(number) {}
    VoidPinDetail::VoidPinDetail(const PinOptionsParser& options) : VoidPinDetail() {}

    PinCapabilities VoidPinDetail::capabilities() const {
        // Void pins support basic functionality. It just won't do you any good.
        return PinCapabilities::Output | PinCapabilities::Input | PinCapabilities::ISR | PinCapabilities::Void;
    }

    void IRAM_ATTR VoidPinDetail::write(int high) {}
    int            VoidPinDetail::read() { return 0; }
    void           VoidPinDetail::setAttr(PinAttributes value) {}
    PinAttributes  VoidPinDetail::getAttr() const { return PinAttributes::None; }

    std::string VoidPinDetail::toString() { return std::string("NO_PIN"); }

}
