// Copyright (c) 2023 Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#ifdef ESP32
#    include "UARTPinDetail.h"
#    include "PinDetail.h"

#    include "../I2SOut.h"
#    include "../Assert.h"

namespace Pins {
    std::vector<bool> UARTIODetail::_claimed(nPins, false);

    UARTIODetail::UARTIODetail(int deviceId, pinnum_t index, const PinOptionsParser& options) :
        PinDetail(index), _capabilities(PinCapabilities::Output | PinCapabilities::UARTIO), _attributes(Pins::PinAttributes::Undefined),
        _readWriteMask(0) {
        Assert(index < nPins, "Pin number is greater than max %d", nPins - 1);
        Assert(!_claimed[index], "Pin is already used.");

        // User defined pin capabilities
        for (auto opt : options) {
            if (opt.is("pu")) {
            } else if (opt.is("pd")) {
            } else if (opt.is("low")) {
                _attributes = _attributes | PinAttributes::ActiveLow;
            } else if (opt.is("high")) {
                // Default: Active HIGH.
            } else {
                Assert(false, "Bad UARTPin option passed to pin %d: %s", int(index), opt());
            }
        }

        _device_id = deviceId;

        //log_info("created uart pin:" << index);
        _claimed[index] = true;

        // readWriteMask is xor'ed with the value to invert it if active low
        _readWriteMask = int(_attributes.has(PinAttributes::ActiveLow));
    }
    // The write will not happen immediately; the data is queued for
    // delivery to the serial shift register chain via DMA and a FIFO

    PinCapabilities UARTIODetail::capabilities() const {
        return PinCapabilities::Output | PinCapabilities::UARTIO;
    }

    void IRAM_ATTR UARTIODetail::write(int high) {
        if (high != _lastWrittenValue) {
            _lastWrittenValue = high;
            if (!_attributes.has(PinAttributes::Output)) {
                log_error(toString());
            }
            Assert(_attributes.has(PinAttributes::Output), "Pin %s cannot be written", toString().c_str());
            int value = _readWriteMask ^ high;
            log_info("Set uart pin:" << high);
        }
    }

    int UARTIODetail::read() {
        return 0;
    }

    void UARTIODetail::setAttr(PinAttributes value) {
        // These two assertions will fail if we do them for index 1/3 (Serial uart). This is because
        // they are initialized by HardwareSerial well before we start our main operations. Best to
        // just ignore them for now, and figure this out later. TODO FIXME!

        // Check the attributes first:
        Assert(value.validateWith(this->_capabilities) || _index == 1 || _index == 3,
               "The requested attributes don't match the capabilities for %s",
               toString().c_str());
        Assert(!_attributes.conflictsWith(value) || _index == 1 || _index == 3,
               "The requested attributes on %s conflict with previous settings",
               toString().c_str());

        _attributes = _attributes | value;

        // If the pin is ActiveLow, we should take that into account here:
        if (value.has(PinAttributes::Output)) {
            //gpio_write(_index, int(value.has(PinAttributes::InitialOn)) ^ _readWriteMask);
        }
    }

    PinAttributes UARTIODetail::getAttr() const {
        return _attributes;
    }

    std::string UARTIODetail::toString() {
        std::string s("uart_channel");
        s += std::to_string(_device_id);
        s += ".";
        s += std::to_string(_index);
        if (_attributes.has(PinAttributes::ActiveLow)) {
            s += ":low";
        }
        if (_attributes.has(PinAttributes::PullUp)) {
            s += ":pu";
        }
        if (_attributes.has(PinAttributes::PullDown)) {
            s += ":pd";
        }
        return s;
    }
}

#endif
