// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "ExtPinDetail.h"

namespace Pins {
    ExtPinDetail::ExtPinDetail(int device, pinnum_t index, const PinOptionsParser& options) :
        PinDetail(index), _device(device), _capabilities(PinCapabilities::Output | PinCapabilities::Input | PinCapabilities::ISR),
        _attributes(Pins::PinAttributes::Undefined) {
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
    }

    PinCapabilities ExtPinDetail::capabilities() const { return PinCapabilities::Input | PinCapabilities::Output | PinCapabilities::ISR; }

    // I/O:
    void ExtPinDetail::write(int high) {
        Assert(_owner != nullptr, "Cannot write to uninitialized pin");
        _owner->writePin(_index, high);
    }

    void ExtPinDetail::synchronousWrite(int high) {
        Assert(_owner != nullptr, "Cannot write to uninitialized pin");
        _owner->writePin(_index, high);
        _owner->flushWrites();
    }

    int ExtPinDetail::read() {
        Assert(_owner != nullptr, "Cannot read from uninitialized pin");
        return _owner->readPin(_index);
    }

    void ExtPinDetail::setAttr(PinAttributes value) {
        // We setup the driver in setAttr. Before this time, the owner might not be valid.

        // Check the attributes first:
        Assert(value.has(PinAttributes::Input) || value.has(PinAttributes::Output), "PCA9539 pins can be used as either input or output.");
        Assert(value.has(PinAttributes::Input) != value.has(PinAttributes::Output), "PCA9539 pins can be used as either input or output.");
        Assert(value.validateWith(this->_capabilities), "Requested attributes do not match the PCA9539 pin capabilities.");
        Assert(!_attributes.conflictsWith(value), "Attributes on this pin have been set before, and there's a conflict.");

        _attributes = value;

        bool activeLow = _attributes.has(PinAttributes::ActiveLow);

        if (_owner == nullptr) {
            auto ext = config->_extenders;
            if (ext != nullptr && ext->_pinDrivers[_device] != nullptr && ext->_pinDrivers[_device]->_driver != nullptr) {
                _owner = ext->_pinDrivers[_device]->_driver;
            } else {
                Assert(false, "Cannot find pin extender definition in configuration for pin pinext%d.%d", _device, _index);
            }

            _owner->claim(_index);
        }

        _owner->setupPin(_index, _attributes);
        _owner->writePin(_index, value.has(PinAttributes::InitialOn));
    }

    PinAttributes ExtPinDetail::getAttr() const { return _attributes; }

    void ExtPinDetail::attachInterrupt(void (*callback)(void*), void* arg, int mode) {
        Assert(_owner != nullptr, "Cannot attach ISR on uninitialized pin");
        _owner->attachInterrupt(_index, callback, arg, mode);
    }
    void ExtPinDetail::detachInterrupt() {
        Assert(_owner != nullptr, "Cannot detach ISR on uninitialized pin");
        _owner->detachInterrupt(_index);
    }

    String ExtPinDetail::toString() {
        auto s = String("pinext") + int(_device) + String(".") + int(_index);
        if (_attributes.has(PinAttributes::ActiveLow)) {
            s += ":low";
        }
        return s;
    }

    ExtPinDetail::~ExtPinDetail() {
        if (_owner) {
            _owner->free(_index);
        }
    }
}
