// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <esp_attr.h>  // IRAM_ATTR
#include <esp32-hal-gpio.h>
#include "Driver/fluidnc_gpio.h"
#include <stdexcept>

#include "GPIOPinDetail.h"
#include "src/Assert.h"
#include "src/Config.h"
#include "src/Machine/EventPin.h"

namespace Pins {
    std::vector<bool> GPIOPinDetail::_claimed(nGPIOPins, false);

    PinCapabilities GPIOPinDetail::GetDefaultCapabilities(pinnum_t index) {
        // See https://randomnerdtutorials.com/esp32-pinout-reference-gpios/ for an overview:
        switch (index) {
            case 0:  // Outputs PWM signal at boot
                return PinCapabilities::Native | PinCapabilities::Input | PinCapabilities::Output | PinCapabilities::PullUp |
                       PinCapabilities::PullDown | PinCapabilities::ADC | PinCapabilities::PWM | PinCapabilities::ISR |
                       PinCapabilities::UART;

            case 1:  // TX pin of Serial0. Note that Serial0 also runs through the Pins framework!
                return PinCapabilities::Native | PinCapabilities::Output | PinCapabilities::Input | PinCapabilities::UART;

            case 3:  // RX pin of Serial0. Note that Serial0 also runs through the Pins framework!
                return PinCapabilities::Native | PinCapabilities::Output | PinCapabilities::Input | PinCapabilities::ISR |
                       PinCapabilities::UART;

            case 5:
            case 9:
            case 10:
            case 16:
            case 17:
            case 18:
            case 19:
            case 21:
            case 22:
            case 23:
            case 29:
                return PinCapabilities::Native | PinCapabilities::Input | PinCapabilities::Output | PinCapabilities::PullUp |
                       PinCapabilities::PullDown | PinCapabilities::PWM | PinCapabilities::ISR | PinCapabilities::UART;

            case 2:  // Normal pins
            case 4:
            case 12:  // Boot fail if pulled high
            case 13:
            case 14:  // Outputs PWM signal at boot
            case 15:  // Outputs PWM signal at boot
            case 27:
            case 32:
            case 33:
                return PinCapabilities::Native | PinCapabilities::Input | PinCapabilities::Output | PinCapabilities::PullUp |
                       PinCapabilities::PullDown | PinCapabilities::ADC | PinCapabilities::PWM | PinCapabilities::ISR |
                       PinCapabilities::UART;

            case 25:
            case 26:
                return PinCapabilities::Native | PinCapabilities::Input | PinCapabilities::Output | PinCapabilities::PullUp |
                       PinCapabilities::PullDown | PinCapabilities::ADC | PinCapabilities::DAC | PinCapabilities::PWM |
                       PinCapabilities::ISR | PinCapabilities::UART;

            case 6:  // SPI flash integrated
            case 7:
            case 8:
            case 11:
                return PinCapabilities::Reserved;

            case 34:  // Input only pins
            case 35:
            case 36:
            case 39:
                return PinCapabilities::Native | PinCapabilities::Input | PinCapabilities::ADC | PinCapabilities::ISR | PinCapabilities::UART;
                break;

            default:  // Not mapped to actual GPIO pins
                return PinCapabilities::None;
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

    PinAttributes GPIOPinDetail::getAttr() const { return _attributes; }

    PinCapabilities GPIOPinDetail::capabilities() const { return _capabilities; }

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

    // This is a callback from the low-level GPIO driver that is invoked after
    // registerEvent() has been called and the pin becomes active.
    void GPIOPinDetail::gpioAction(int gpio_num, void* arg, bool active) {
        EventPin* obj = static_cast<EventPin*>(arg);
        obj->trigger(active);
    }

    void GPIOPinDetail::registerEvent(EventPin* obj) {
        gpio_set_action(_index, gpioAction, (void*)obj, _attributes.has(Pin::Attr::ActiveLow));
    }

    std::string GPIOPinDetail::toString() {
        std::string s("gpio.");
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
