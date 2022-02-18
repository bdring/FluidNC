// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#ifdef ESP32
#    include "I2SOPinDetail.h"

#    include "../I2SOut.h"
#    include "../Assert.h"

extern "C" void __digitalWrite(pinnum_t pin, uint8_t val);

namespace Pins {
    std::vector<bool> I2SOPinDetail::_claimed(nI2SOPins, false);

    I2SOPinDetail::I2SOPinDetail(pinnum_t index, const PinOptionsParser& options) :
        PinDetail(index), _capabilities(PinCapabilities::Output | PinCapabilities::I2S), _attributes(Pins::PinAttributes::Undefined),
        _readWriteMask(0) {
        Assert(index < nI2SOPins, "Pin number is greater than max %d", nI2SOPins - 1);
        Assert(!_claimed[index], "Pin is already used.");
        // User defined pin capabilities
        for (auto opt : options) {
            if (opt.is("low")) {
                _attributes = _attributes | PinAttributes::ActiveLow;
            } else if (opt.is("high")) {
                // Default: Active HIGH.
            } else {
                Assert(false, "Unsupported I2SO option '%s'", opt());
            }
        }
        _claimed[index] = true;

        // readWriteMask is xor'ed with the value to invert it if active low
        _readWriteMask = _attributes.has(PinAttributes::ActiveLow);
    }

    PinCapabilities I2SOPinDetail::capabilities() const { return PinCapabilities::Output | PinCapabilities::I2S; }

    // The write will not happen immediately; the data is queued for
    // delivery to the serial shift register chain via DMA and a FIFO
    void IRAM_ATTR I2SOPinDetail::write(int high) {
        int value = _readWriteMask ^ high;
        i2s_out_write(_index, value);
    }

    // Write and wait for completion.  Not suitable for use from an ISR
    void I2SOPinDetail::synchronousWrite(int high) {
        write(high);
        i2s_out_push();
        i2s_out_delay();
    }

    int I2SOPinDetail::read() {
        auto raw = i2s_out_read(_index);
        return raw ^ _readWriteMask;
    }

    void I2SOPinDetail::setAttr(PinAttributes value) {
        // Check the attributes first:
        Assert(!value.has(PinAttributes::Input), "I2SO pins cannot be used as input");
        Assert(value.validateWith(this->_capabilities), "Requested attributes do not match the I2SO pin capabilities");
        Assert(!_attributes.conflictsWith(value), "Attributes on this pin have been set before, and there's a conflict.");

        _attributes = _attributes | value;

        // I2S out pins cannot be configured, hence there
        // is nothing to do here for them. We basically
        // just check for conflicts above...

        // If the pin is ActiveLow, we should take that into account here:
        i2s_out_write(_index, value.has(PinAttributes::InitialOn) ^ _readWriteMask);
    }

    PinAttributes I2SOPinDetail::getAttr() const { return _attributes; }

    String I2SOPinDetail::toString() {
        auto s = String("I2SO.") + int(_index);
        if (_attributes.has(PinAttributes::ActiveLow)) {
            s += ":low";
        }
        return s;
    }
}

#endif
