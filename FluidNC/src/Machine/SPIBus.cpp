// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "SPIBus.h"

#include <SPI.h>

namespace Machine {
    void SPIBus::validate() const {
        if (_cs.defined() || _miso.defined() || _mosi.defined() || _sck.defined()) {
            Assert(_cs.defined(), "SPI CS pin should be configured once");
            Assert(_miso.defined(), "SPI MISO pin should be configured once");
            Assert(_mosi.defined(), "SPI MOSI pin should be configured once");
            Assert(_sck.defined(), "SPI SCK pin should be configured once");
        }
    }

    void SPIBus::init() {
        if (_cs.defined()) {  // validation ensures the rest is also defined.
            log_info("SPI SCK:" << _sck.name() << " MOSI:" << _mosi.name() << " MISO:" << _miso.name() << " CS:" << _cs.name());

            _cs.setAttr(Pin::Attr::Output);

            auto csPin   = _cs.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
            auto mosiPin = _mosi.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
            auto sckPin  = _sck.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
            auto misoPin = _miso.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);

            // Start the SPI bus with the pins defined here.  Once it has been started,
            // those pins "stick" and subsequent attempts to restart it with defaults
            // for the miso, mosi, and sck pins are ignored
            SPI.begin(sckPin, misoPin, mosiPin, csPin);
        }
    }

    void SPIBus::group(Configuration::HandlerBase& handler) {
        handler.item("cs", _cs);
        handler.item("miso", _miso);
        handler.item("mosi", _mosi);
        handler.item("sck", _sck);
    }

    // XXX it would be nice to have some way to turn off SPI entirely
    void SPIBus::afterParse() {
        if (_cs.undefined()) {
            _cs = Pin::create("gpio.5");
        }
        if (_miso.undefined()) {
            _miso = Pin::create("gpio.19");
        }
        if (_mosi.undefined()) {
            _mosi = Pin::create("gpio.23");
        }
        if (_sck.undefined()) {
            _sck = Pin::create("gpio.18");
        }
    }
}
