// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"

#include "SDCard.h"
#include "Machine/MachineConfig.h"
#include "Print.h"
#include "Report.h"

#include <SD.h>
#include <SPI.h>

SDCard::SDCard() : _state(State::Idle) {}

void SDCard::listDir(fs::FS& fs, const char* dirname, size_t levels, Print& client) {
    //char temp_filename[128]; // to help filter by extension	TODO: 128 needs a definition based on something
    File root = fs.open(dirname);
    if (!root) {
        report_status_message(Error::FsFailedOpenDir, client);
        return;
    }
    if (!root.isDirectory()) {
        report_status_message(Error::FsDirNotFound, client);
        return;
    }
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            if (levels) {
                listDir(fs, file.name(), levels - 1, client);
            }
        } else {
            allClients << "[FILE:" << file.name() << "|SIZE:" << file.size() << '\n';
        }
        file = root.openNextFile();
    }
}

// NotPresent can mean several different things:
// 1. The hardware does not support an SD card
// 2. The system configuration does not include the SD card
// 3. The SD card is not plugged in and there is a detect pin to tell us that
// 4. The SD card is not plugged in and we have to discover that by trying to read it.
// 5. The SD card is plugged in but its filesystem cannot be read
SDCard::State SDCard::test_or_open(bool refresh) {
    auto spiConfig = config->_spi;

    if (spiConfig == nullptr || !spiConfig->defined()) {
        //log_debug("SPI not defined");
        return SDCard::State::NotPresent;
    }

    if (spiConfig == nullptr || _cs.undefined()) {
        //log_debug("SD cs not defined");
        return SDCard::State::NotPresent;
    }

    //no need to go further if SD detect is not correct
    if (_cardDetect.defined() && !_cardDetect.read()) {
        _state = SDCard::State::NotPresent;
        return _state;
    }

    //if busy doing something return state
    if (_state >= SDCard::State::Busy) {
        return _state;
    }

    if (!refresh) {
        return _state;  //to avoid refresh=true + busy to reset SD and waste time
    }

    //SD is idle or not detected, let see if still the case
    SD.end();

    _state = SDCard::State::NotPresent;

    auto csPin = _cs.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);

    //refresh content if card was removed
    if (SD.begin(csPin, SPI, SPIfreq, "/sd", 2)) {
        if (SD.cardSize() > 0) {
            _state = SDCard::State::Idle;
        }
    }
    return _state;
}

SDCard::State SDCard::begin(SDCard::State newState) {
    SDCard::State oldState = test_or_open(true);
    if (oldState == SDCard::State::Idle) {
        _state = newState;
    }
    return oldState;
}

SDCard::State SDCard::get_state() {
    return test_or_open(false);
}

void SDCard::end() {
    SD.end();
    _state = State::Idle;
}

void SDCard::init() {
    static bool init_message = true;  // used to show messages only once.

    if (_cs.defined()) {
        if (!config->_spi->defined()) {
            log_error("SD needs SPI defined");
        } else {
            if (init_message) {
                _cardDetect.report("SD Card Detect");
                init_message = false;
            }
            log_info("SD Card cs_pin:" << _cs.name() << " dectect:" << _cardDetect.name());
        }
    }

    _cs.setAttr(Pin::Attr::Output);
    _cardDetect.setAttr(Pin::Attr::Output);
}

void SDCard::afterParse() {
    // if (_cs.undefined()) {
    //     _cs = Pin::create("gpio.5");
    // }
}

SDCard::~SDCard() {}
