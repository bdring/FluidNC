// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <esp_attr.h>  // IRAM_ATTR
#include <esp32-hal-gpio.h>
#include "Driver/fluidnc_gpio.h"
#include <stdexcept>

#include "GPIOPinDetail.h"
#include "../Assert.h"
#include "../Config.h"

namespace Pins {
    std::vector<bool> GPIOPinDetail::_claimed(nGPIOPins, false);

    PinCapabilities GPIOPinDetail::GetDefaultCapabilities(pinnum_t index) {
        // See https://randomnerdtutorials.com/esp32-pinout-reference-gpios/ for an overview:
        switch (index) {
            case 0:  // Outputs PWM signal at boot
                return PinCapabilities::Native | PinCapabilities::Input | PinCapabilities::Output | PinCapabilities::PullUp |
                       PinCapabilities::PullDown | PinCapabilities::ADC | PinCapabilities::PWM | PinCapabilities::ISR |
                       PinCapabilities::UART;
                break;
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 10:
            case 11:
            case 12:
            case 13:
            case 14:
            case 15:
            case 16:
            case 17:
            case 18:
            case 21:
            case 35:
            case 36:
            case 37:
            case 38:
            case 39:
            case 40:
            case 41:
            case 42:
            case 45:
            case 46:
            case 47:
            case 48:
                return PinCapabilities::Native | PinCapabilities::Input | PinCapabilities::Output | PinCapabilities::PullUp |
                       PinCapabilities::PullDown | PinCapabilities::ADC | PinCapabilities::PWM | PinCapabilities::ISR |
                       PinCapabilities::UART;
                break;
            case 19:
            case 20:
            case 43:
            case 44:
                return PinCapabilities::Reserved;
                break;
            default:  // Not mapped to actual GPIO pins
                return PinCapabilities::None;
                break;
        }
    }

    GPIOPinDetail::GPIOPinDetail(pinnum_t index, PinOptionsParser options) :
        PinDetail(index), _capabilities(GetDefaultCapabilities(index)), _attributes(Pins::PinAttributes::Undefined), _readWriteMask(0) {
        // NOTE:
        //
        // RAII is very important here! If we throw an exception in the constructor, the resources
        // that were allocated by the constructor up to that point _MUST_ be freed! Otherwise, you
        // WILL get into trouble.

        Assert(index < nGPIOPins, "Pin number is greater than max %d", nGPIOPins - 1);
        Assert(_capabilities != PinCapabilities::Reserved, "Unusable GPIO");
        Assert(_capabilities != PinCapabilities::None, "Unavailable GPIO");
        Assert(!_claimed[index], "Pin is already used.");

        // User defined pin capabilities
        for (auto opt : options) {
            if (opt.is("pu")) {
                if (_capabilities.has(PinCapabilities::PullUp)) {
                    _attributes = _attributes | PinAttributes::PullUp;
                } else {
                    log_warn(toString() << " does not support :pu attribute");
                }

            } else if (opt.is("pd")) {
                if (_capabilities.has(PinCapabilities::PullDown)) {
                    _attributes = _attributes | PinAttributes::PullDown;
                } else {
                    log_warn(toString() << " does not support :pd attribute");
                }
            } else if (opt.is("low")) {
                _attributes = _attributes | PinAttributes::ActiveLow;
            } else if (opt.is("high")) {
                // Default: Active HIGH.
            } else {
                Assert(false, "Bad GPIO option passed to pin %d: %s", int(index), opt());
            }
        }
        _claimed[index] = true;

        // readWriteMask is xor'ed with the value to invert it if active low
        _readWriteMask = int(_attributes.has(PinAttributes::ActiveLow));
    }

    PinAttributes GPIOPinDetail::getAttr() const {
        return _attributes;
    }

    PinCapabilities GPIOPinDetail::capabilities() const {
        return _capabilities;
    }

    void IRAM_ATTR GPIOPinDetail::write(int high) {
        if (high != _lastWrittenValue) {
            _lastWrittenValue = high;
            if (!_attributes.has(PinAttributes::Output)) {
                log_error(toString());
            }
            Assert(_attributes.has(PinAttributes::Output), "Pin %s cannot be written", toString().c_str());
            int value = _readWriteMask ^ high;
            gpio_write(_index, value);
        }
    }
    int IRAM_ATTR GPIOPinDetail::read() {
        auto raw = gpio_read(_index);
        return raw ^ _readWriteMask;
    }

    void GPIOPinDetail::setAttr(PinAttributes value) {
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
            gpio_write(_index, int(value.has(PinAttributes::InitialOn)) ^ _readWriteMask);
        }

        gpio_mode(_index,
                  value.has(PinAttributes::Input),
                  value.has(PinAttributes::Output),
                  _attributes.has(PinAttributes::PullUp),
                  _attributes.has(PinAttributes::PullDown),
                  false);  // We do not have an OpenDrain attribute yet
    }

    void GPIOPinDetail::attachInterrupt(void (*callback)(void*), void* arg, int mode) {
        Assert(_attributes.has(PinAttributes::ISR), "Pin %s does not support interrupts", toString().c_str());
        ::attachInterruptArg(_index, callback, arg, mode);
    }

    void GPIOPinDetail::detachInterrupt() {
        Assert(_attributes.has(PinAttributes::ISR), "Pin %s does not support interrupts");
        ::detachInterrupt(_index);
    }

    String GPIOPinDetail::toString() {
        auto s = String("gpio.") + int(_index);
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
