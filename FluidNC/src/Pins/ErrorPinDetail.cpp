// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ErrorPinDetail.h"
#include "../Config.h"
#include "../Assert.h"

namespace Pins {
    ErrorPinDetail::ErrorPinDetail(std::string_view descr) : PinDetail(0), _description(descr) {}

    PinCapabilities ErrorPinDetail::capabilities() const {
        return PinCapabilities::Error;
    }

#ifdef ESP32
    void IRAM_ATTR ErrorPinDetail::write(int high) {
        log_error("Cannot write to pin " << _description.c_str() << ". The config is incorrect.");
    }
    int ErrorPinDetail::read() {
        log_error("Cannot read from pin " << _description.c_str() << ". The config is incorrect.");
        return false;
    }
    void ErrorPinDetail::setAttr(PinAttributes value, uint32_t frequency) {
        log_error("Cannot set mode on pin " << _description.c_str() << ". The config is incorrect.");
    }

#else
    void IRAM_ATTR ErrorPinDetail::write(int high) {
        Assert(false, "Cannot write to an error pin.");
    }
    int ErrorPinDetail::read() {
        Assert(false, "Cannot read from an error pin.");
        return false;
    }
    void ErrorPinDetail::setAttr(PinAttributes value, uint32_t frequency) { /* Fine, this won't get you anywhere. */
    }

#endif

    PinAttributes ErrorPinDetail::getAttr() const {
        return PinAttributes::None;
    }

    std::string ErrorPinDetail::toString() {
        std::string s("ERROR_PIN (for ");
        return s + _description + ")";
    }
}
