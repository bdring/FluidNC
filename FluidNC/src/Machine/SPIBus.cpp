// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Copyright (c) 2021 -  Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "SPIBus.h"

#include <SPI.h>

namespace Machine {
    void SPIBus::validate() const {
        if (_miso.defined() || _mosi.defined() || _sck.defined()) {
            Assert(_miso.defined(), "SPI MISO pin should be configured once");
            Assert(_mosi.defined(), "SPI MOSI pin should be configured once");
            Assert(_sck.defined(), "SPI SCK pin should be configured once");
        }
    }

    void SPIBus::init() {
        if (_miso.defined() || _mosi.defined() || _sck.defined()) {  // validation ensures the rest is also defined.
            
            try {
                auto mosiPin = _mosi.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
                auto sckPin  = _sck.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
                auto misoPin = _miso.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);
                SPI.begin(sckPin, misoPin, mosiPin);  // CS is defined by each device
                log_info("SPI SCK:" << _sck.name() << " MOSI:" << _mosi.name() << " MISO:" << _miso.name());
                _defined = true;
            } catch (const AssertionFailed& ex) {
                _defined = false;
                log_error(ex.msg);
                log_error("SPI Pins failed capability  check SCK:" << _sck.name() << " MOSI:" << _mosi.name() << " MISO:" << _miso.name());
            }

        } else {
            _defined = false;
            log_info("SPI not defined");
        }
    }

    void SPIBus::group(Configuration::HandlerBase& handler) {
        handler.item("miso", _miso);
        handler.item("mosi", _mosi);
        handler.item("sck", _sck);
    }

    // XXX it would be nice to have some way to turn off SPI entirely
    void SPIBus::afterParse() {
    }

    bool SPIBus::defined() { return _defined; }
}
