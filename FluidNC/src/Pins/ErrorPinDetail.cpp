// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ErrorPinDetail.h"
#include "Config.h"
namespace Pins {
    ErrorPinDetail::ErrorPinDetail(std::string_view descr) : PinDetail(0), _description(descr) {
        _name = "ERROR_PIN (for ";
        _name += _description;
        _name += ")";
    }

    PinCapabilities ErrorPinDetail::capabilities() const {
        return PinCapabilities::Error;
    }

    void IRAM_ATTR ErrorPinDetail::write(bool high) {
        log_error("Cannot write to pin " << _description.c_str() << ". The config is incorrect.");
    }
    bool ErrorPinDetail::read() {
        log_error("Cannot read from pin " << _description.c_str() << ". The config is incorrect.");
        return false;
    }
    void ErrorPinDetail::setAttr(PinAttributes value, uint32_t frequency) {
        log_error("Cannot set mode on pin " << _description.c_str() << ". The config is incorrect.");
    }

    PinAttributes ErrorPinDetail::getAttr() const {
        return PinAttributes::None;
    }
}
