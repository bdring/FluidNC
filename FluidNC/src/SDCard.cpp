// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"

#include "SDCard.h"
#include "Machine/MachineConfig.h"
#include "Channel.h"
#include "Report.h"

#include "Driver/sdspi.h"
#include "src/SettingsDefinitions.h"
#include "FluidPath.h"

SDCard::SDCard() : _state(State::Idle) {}

void SDCard::init() {
    static bool init_message = true;  // used to show messages only once.
    pinnum_t    csPin;
    int         csFallback;

    if (_cs.defined()) {
        if (!config->_spi->defined()) {
            log_error("SD needs SPI defined");
        } else {
            if (init_message) {
                _cardDetect.report("SD Card Detect");
                init_message = false;
            }
            log_info("SD Card cs_pin:" << _cs.name() << " detect:" << _cardDetect.name());
        }
        _cs.setAttr(Pin::Attr::Output);
        csPin = _cs.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
    } else if ((csFallback = sd_fallback_cs->get()) != -1) {
        csPin = static_cast<pinnum_t>(csFallback);
        log_info("Using fallback CS pin " << int(csPin));
    } else {
        log_info("No SD Card CS Pin");
        log_info("See http://wiki.fluidnc.com/en/config/sd_card#sdfallbackcs-access-sd-without-a-config-file");
        return;
    }

    if (_cardDetect.defined()) {
        _cardDetect.setAttr(Pin::Attr::Input);
        auto cdPin = _cardDetect.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
        sd_init_slot(csPin, cdPin);
    } else {
        sd_init_slot(csPin);
    }
}

void SDCard::afterParse() {
    // if (_cs.undefined()) {
    //     _cs = Pin::create("gpio.5");
    // }
}

SDCard::~SDCard() {}
