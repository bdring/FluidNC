// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#ifdef ESP32
#    include "I2SOPinDetail.h"

#    include "Driver/i2s_out.h"  // i2s_out_write() etc
#    include "../Assert.h"
#    include <esp_attr.h>  // IRAM_ATTR

namespace Pins {
    std::vector<bool> I2SOPinDetail::_claimed(nI2SOPins, false);

    I2SOPinDetail::I2SOPinDetail(pinnum_t index, const PinOptionsParser& options) :
        PinDetail(index), _capabilities(PinCapabilities::Output | PinCapabilities::I2S), _attributes(Pins::PinAttributes::Undefined) {
        Assert(index < nI2SOPins, "Pin number is greater than max %d", nI2SOPins - 1);
        Assert(!_claimed[index], "Pin is already used.");
        // User defined pin capabilities
        for (auto opt : options) {
            if (opt.is("low")) {
                _attributes = _attributes | PinAttributes::ActiveLow;
            } else if (opt.is("high")) {
                // Default: Active HIGH.
            } else {
                Assert(false, "Unsupported I2SO option '%.*s'", static_cast<int>(opt().length()), opt().data());
            }
        }
        _claimed[index] = true;

        // readWriteMask is xor'ed with the value to invert it if active low
        _inverted = _attributes.has(PinAttributes::ActiveLow);
    }

    PinCapabilities I2SOPinDetail::capabilities() const {
        return PinCapabilities::Output | PinCapabilities::I2S;
    }

    // The write will not happen immediately; the data is queued for
    // delivery to the serial shift register chain via DMA and a FIFO
    void IRAM_ATTR I2SOPinDetail::write(int high) {
        if (high != _lastWrittenValue) {
            _lastWrittenValue = high;
            i2s_out_write(_index, _inverted ^ (bool)high);
        }
    }

    // Write and wait for completion.  Not suitable for use from an ISR
    // cppcheck-suppress unusedFunction
    void IRAM_ATTR I2SOPinDetail::synchronousWrite(int high) {
        if (high != _lastWrittenValue) {
            _lastWrittenValue = high;

            i2s_out_write(_index, _inverted ^ (bool)high);
            i2s_out_delay();
        }
    }

    int I2SOPinDetail::read() {
        auto raw = i2s_out_read(_index);
        return (bool)raw ^ _inverted;
    }

    void I2SOPinDetail::setAttr(PinAttributes value, uint32_t frequency) {
        // The Arduino framework encodes OUTPUT as OUTPUT | INPUT.  We can't do the input part.
        if (value.has(PinAttributes::Output) && value.has(PinAttributes::Input)) {
            value = PinAttributes::Output;
        }
        Assert(!value.has(PinAttributes::Input), "I2SO pins cannot be used as input");
        Assert(value.validateWith(this->_capabilities), "Requested attributes do not match the I2SO pin capabilities");
        Assert(!_attributes.conflictsWith(value), "Attributes on this pin have been set before, and there's a conflict.");

        _attributes = _attributes | value;

        // I2S out pins cannot be configured, hence there
        // is nothing to do here for them. We basically
        // just check for conflicts above...

        // Set the initial value of the pin per the configuration
        i2s_out_write(_index, value.has(PinAttributes::InitialOn) ^ _inverted);
    }

    PinAttributes I2SOPinDetail::getAttr() const {
        return _attributes;
    }

    std::string I2SOPinDetail::toString() {
        std::string s("I2SO.");
        s += std::to_string(_index);
        if (_attributes.has(PinAttributes::ActiveLow)) {
            s += ":low";
        }
        return s;
    }
}

#endif
