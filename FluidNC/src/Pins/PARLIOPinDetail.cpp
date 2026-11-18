// Copyright (c) 2021 - Stefan de Bruijn
// Copyright (c) 2026 - FluidNC contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#if MAX_N_PARLIO
#    include "PARLIOPinDetail.h"

namespace Pins {
    std::vector<bool> PARLIOPinDetail::_claimed(nPARLIOPins, false);

    PARLIOPinDetail::PARLIOPinDetail(pinnum_t index, const PinOptionsParser& options) :
        PinDetail(index), _capabilities(PinCapabilities::Output | PinCapabilities::PARLIO), _attributes(Pins::PinAttributes::Undefined) {
        Assert(index < nPARLIOPins, "Pin number is greater than max %d", nPARLIOPins - 1);
        Assert(!_claimed[index], "Pin is already used");

        // @pin_namespace parlio
        _name = "PARLIO.";
        _name += std::to_string(_index);

        // User defined pin capabilities
        for (auto opt : options) {
            if (opt.is("low")) {
                _attributes = _attributes | PinAttributes::ActiveLow;
                _name += ":low";
            } else if (opt.is("high")) {
                // Default: Active HIGH.
            } else {
                Assert(false, "Unsupported PARLIO option '%.*s'", static_cast<int>(opt().length()), opt().data());
            }
        }
        _claimed[index] = true;

        _inverted = _attributes.has(PinAttributes::ActiveLow);
    }

    PinCapabilities PARLIOPinDetail::capabilities() const {
        return PinCapabilities::Output | PinCapabilities::PARLIO;
    }

    void PARLIOPinDetail::write(bool high) {
        _lastWrittenValue = high;
    }

    void PARLIOPinDetail::synchronousWrite(bool high) {
        _lastWrittenValue = high;
    }

    bool PARLIOPinDetail::read() {
        return _lastWrittenValue;
    }

    void PARLIOPinDetail::setAttr(PinAttributes value, uint32_t frequency) {
        // The Arduino framework encodes OUTPUT as OUTPUT | INPUT.  We can't do the input part.
        if (value.has(PinAttributes::Output) && value.has(PinAttributes::Input)) {
            value = PinAttributes::Output;
        }
        Assert(!value.has(PinAttributes::Input), "PARLIO pins cannot be used as input");
        Assert(value.validateWith(this->_capabilities), "Requested attributes do not match the PARLIO pin capabilities");
        Assert(!_attributes.conflictsWith(value), "Attributes on this pin have been set before, and there's a conflict");

        _attributes = _attributes | value;
    }

    PinAttributes PARLIOPinDetail::getAttr() const {
        return _attributes;
    }
}
#endif
