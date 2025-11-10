// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "VoidPinDetail.h"

namespace Pins {
    VoidPinDetail::VoidPinDetail(pinnum_t number) : PinDetail(number) {
        _name = "NO PIN";
    }
    VoidPinDetail::VoidPinDetail(const PinOptionsParser& options) : VoidPinDetail() {}

    // cppcheck-suppress unusedFunction
    PinCapabilities VoidPinDetail::capabilities() const {
        // Void pins support basic functionality. It just won't do you any good.
        return PinCapabilities::Output | PinCapabilities::Input | PinCapabilities::ISR | PinCapabilities::Void;
    }

    // cppcheck-suppress unusedFunction
    void IRAM_ATTR VoidPinDetail::write(bool high) {}
    // cppcheck-suppress unusedFunction
    bool VoidPinDetail::read() {
        return 0;
    }
    // cppcheck-suppress unusedFunction
    void VoidPinDetail::setAttr(PinAttributes value, uint32_t frequency) {}
    // cppcheck-suppress unusedFunction
    PinAttributes VoidPinDetail::getAttr() const {
        return PinAttributes::None;
    }
}
