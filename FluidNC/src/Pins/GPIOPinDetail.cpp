// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#include "Driver/fluidnc_gpio.h"
#include "GPIOPinDetail.h"
#include "Machine/EventPin.h"
#include "Protocol.h"
#include <stdexcept>

namespace Pins {
    std::vector<bool> GPIOPinDetail::_claimed(MAX_N_GPIO, false);

    void GPIOPinDetail::setDriveStrength(uint8_t n, PinAttributes attr) {
        Assert(_capabilities.has(PinCapabilities::Output), "Drive strength only applies to output pins");
        _attributes    = _attributes | attr;
        _driveStrength = n;
    }

    GPIOPinDetail::GPIOPinDetail(pinnum_t index, PinOptionsParser options) :
        PinDetail(index), _capabilities(GetDefaultCapabilities(index)), _attributes(Pins::PinAttributes::Undefined) {
        // NOTE:
        //
        // RAII is very important here! If we throw an exception in the constructor, the resources
        // that were allocated by the constructor up to that point _MUST_ be freed! Otherwise, you
        // WILL get into trouble.

        Assert(index < MAX_N_GPIO, "Pin number is greater than max %d", MAX_N_GPIO - 1);
        Assert(_capabilities != PinCapabilities::Reserved, "Unusable GPIO");
        Assert(_capabilities != PinCapabilities::None, "Unavailable GPIO");
        Assert(!_claimed[index], "Pin is already used");

        _name = "gpio.";
        _name += std::to_string(_index);

        // User defined pin capabilities
        for (auto opt : options) {
            if (opt.is("pu")) {
                if (_capabilities.has(PinCapabilities::PullUp)) {
                    _attributes = _attributes | PinAttributes::PullUp;
                    _name += ":pu";
                } else {
                    log_config_error(name() << " does not support :pu attribute");
                }

            } else if (opt.is("pd")) {
                if (_capabilities.has(PinCapabilities::PullDown)) {
                    _attributes = _attributes | PinAttributes::PullDown;
                    _name += ":pd";
                } else {
                    log_config_error(name() << " does not support :pd attribute");
                }
            } else if (opt.is("low")) {
                _attributes = _attributes | PinAttributes::ActiveLow;
                _name += ":low";
            } else if (opt.is("high")) {
                // Default: Active HIGH.
            } else if (opt.is("ds0")) {
                setDriveStrength(0, PinAttributes::DS0);
                _name += ":ds0";
            } else if (opt.is("ds1")) {
                setDriveStrength(1, PinAttributes::DS1);
                _name += ":ds1";
            } else if (opt.is("ds2")) {
                setDriveStrength(2, PinAttributes::DS2);
                _name += ":ds2";
            } else if (opt.is("ds3")) {
                setDriveStrength(3, PinAttributes::DS3);
                _name += ":ds3";
            } else {
                Assert(false, "Bad GPIO option passed to pin %d: %.*s", int(index), static_cast<int>(opt().length()), opt().data());
            }
        }
        if (_driveStrength != -1) {
            gpio_drive_strength(index, _driveStrength);
        }
        _claimed[index] = true;

        // readWriteMask is xor'ed with the value to invert it if active low
        _inverted = _attributes.has(PinAttributes::ActiveLow);
    }

    PinAttributes GPIOPinDetail::getAttr() const {
        return _attributes;
    }

    PinCapabilities GPIOPinDetail::capabilities() const {
        return _capabilities;
    }

    void IRAM_ATTR GPIOPinDetail::write(bool high) {
        if (high != _lastWrittenValue) {
            _lastWrittenValue = high;
            if (!_attributes.has(PinAttributes::Output)) {
                log_error(name());
            }
            Assert(_attributes.has(PinAttributes::Output), "Pin %s cannot be written", name());
            bool value = _inverted ^ (bool)high;
            gpio_write(_index, value);
        }
    }
    bool IRAM_ATTR GPIOPinDetail::read() {
        auto raw = gpio_read(_index);
        return (bool)raw ^ _inverted;
    }

    void GPIOPinDetail::setAttr(PinAttributes value, uint32_t frequency) {
        // These two assertions will fail if we do them for index 1/3 (Serial uart). This is because
        // they are initialized by HardwareSerial well before we start our main operations. Best to
        // just ignore them for now, and figure this out later. TODO FIXME!

        // Check the attributes first:
        Assert(value.validateWith(this->_capabilities) || _index == 1 || _index == 3,
               "The requested attributes don't match the capabilities for %s",
               name());
        Assert(!_attributes.conflictsWith(value) || _index == 1 || _index == 3,
               "The requested attributes on %s conflict with previous settings",
               name());

        _attributes = _attributes | value;

        if (value.has(PinAttributes::PWM)) {
            _pwm = new PwmPin(_index, _attributes.has(PinAttributes::ActiveLow), frequency);
            // _pwm->setDuty(0);  // Unnecessary since new PwmPins start at 0 duty
            return;
        }

        // If the pin is ActiveLow, we should take that into account here:
        if (value.has(PinAttributes::Output)) {
            gpio_write(_index, int(value.has(PinAttributes::InitialOn)) ^ _inverted);
        }

        gpio_mode(_index,
                  value.has(PinAttributes::Input),
                  value.has(PinAttributes::Output),
                  _attributes.has(PinAttributes::PullUp),
                  _attributes.has(PinAttributes::PullDown),
                  false);  // We do not have an OpenDrain attribute yet

        // setAttr can be used to set the drive strength, which is normally
        // set when the pin is created
        if (value.has(PinAttributes::DS0)) {
            _driveStrength = 0;
        } else if (value.has(PinAttributes::DS1)) {
            _driveStrength = 1;
        } else if (value.has(PinAttributes::DS2)) {
            _driveStrength = 2;
        } else if (value.has(PinAttributes::DS3)) {
            _driveStrength = 3;
        }

        if (_driveStrength != -1) {
            gpio_drive_strength(_index, _driveStrength);
        }
    }

    void IRAM_ATTR GPIOPinDetail::setDuty(uint32_t duty) {
        _pwm->setDuty(duty);
    }

    void GPIOPinDetail::registerEvent(InputPin* obj) {
        gpio_set_event(_index, reinterpret_cast<void*>(obj), _attributes.has(Pin::Attr::ActiveLow));
    }

}
