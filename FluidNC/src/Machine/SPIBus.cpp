// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Copyright (c) 2021 -  Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "SPIBus.h"
#include "Driver/spi.h"
#include "src/SettingsDefinitions.h"

namespace Machine {
    void SPIBus::validate() {
        if (_miso.defined() || _mosi.defined() || _sck.defined()) {
            Assert(_miso.defined(), "SPI MISO pin should be configured once");
            Assert(_mosi.defined(), "SPI MOSI pin should be configured once");
            Assert(_sck.defined(), "SPI SCK pin should be configured once");
        }
    }

    void SPIBus::init() {
        pinnum_t mosiPin = 23;
        pinnum_t misoPin = 19;
        pinnum_t sckPin  = 18;

        if (_miso.defined() || _mosi.defined() || _sck.defined()) {  // validation ensures the rest is also defined.
            log_info("SPI SCK:" << _sck.name() << " MOSI:" << _mosi.name() << " MISO:" << _miso.name());

            mosiPin = _mosi.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
            sckPin  = _sck.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
            misoPin = _miso.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);
        } else {
            if (sd_fallback_cs->get() == -1) {
                log_debug("SPI not defined");
                return;
            }
            log_info("Using default SPI pins");
        }
        // Init in DMA mode
        if (!spi_init_bus(sckPin, misoPin, mosiPin, true)) {
            log_error("SPIBus init failed");
            return;
        }
        _defined = true;
    }

    void SPIBus::deinit() {
        spi_deinit_bus();
    }

    void SPIBus::group(Configuration::HandlerBase& handler) {
        handler.item("miso_pin", _miso);
        handler.item("mosi_pin", _mosi);
        handler.item("sck_pin", _sck);
    }

    // XXX it would be nice to have some way to turn off SPI entirely
    void SPIBus::afterParse() {
        // if (_miso.undefined()) {
        //     _miso = Pin::create("gpio.19");
        // }
        // if (_mosi.undefined()) {
        //     _mosi = Pin::create("gpio.23");
        // }
        // if (_sck.undefined()) {
        //     _sck = Pin::create("gpio.18");
        // }
    }

    bool SPIBus::defined() {
        return _defined;
    }
}
