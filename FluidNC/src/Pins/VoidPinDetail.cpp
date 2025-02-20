// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <esp_attr.h>  // IRAM_ATTR
#include "VoidPinDetail.h"

namespace Pins {
    VoidPinDetail::VoidPinDetail(int number) : PinDetail(number) {}
    VoidPinDetail::VoidPinDetail(const PinOptionsParser& options) : VoidPinDetail() {}

    // cppcheck-suppress unusedFunction
    PinCapabilities VoidPinDetail::capabilities() const {
        // Void pins support basic functionality. It just won't do you any good.
        return PinCapabilities::Output | PinCapabilities::Input | PinCapabilities::ISR | PinCapabilities::Void;
    }

    // cppcheck-suppress unusedFunction
    void IRAM_ATTR VoidPinDetail::write(int high) {}
    // cppcheck-suppress unusedFunction
    int VoidPinDetail::read() {
        return 0;
    }
    // cppcheck-suppress unusedFunction
    void VoidPinDetail::setAttr(PinAttributes value, uint32_t frequency) {}
    // cppcheck-suppress unusedFunction
    PinAttributes VoidPinDetail::getAttr() const {
        return PinAttributes::None;
    }

    // cppcheck-suppress unusedFunction
    std::string VoidPinDetail::toString() {
        return std::string("NO_PIN");
    }
}
