// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"

#include "SDCard.h"
#include "Machine/MachineConfig.h"
#include "Report.h"

#include <FS.h>
#include <SD.h>
#include <SPI.h>

class SDCard::FileWrap {
public:
    FileWrap() : _file(nullptr) {}
    File _file;
};

SDCard::SDCard() :
    _pImpl(new FileWrap()), _current_line_number(0), _state(State::Idle), _readyNext(false), _client(CLIENT_SERIAL),
    _auth_level(WebUI::AuthenticationLevel::LEVEL_GUEST) {}

void SDCard::listDir(fs::FS& fs, const char* dirname, uint8_t levels, uint8_t client) {
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
            _sendf(CLIENT_ALL, "[FILE:%s|SIZE:%d]\r\n", file.name(), file.size());
        }
        file = root.openNextFile();
    }
}

bool SDCard::openFile(fs::FS& fs, const char* path) {
    _pImpl->_file = fs.open(path);
    if (!_pImpl->_file) {
        //report_status_message(Error::FsFailedRead, CLIENT_SERIAL);
        return false;
    }
    _state               = State::BusyPrinting;
    _readyNext           = false;  // this will get set to true when an "ok" message is issued
    _current_line_number = 0;
    return true;
}

bool SDCard::closeFile() {
    _state               = State::Idle;
    _readyNext           = false;
    _current_line_number = 0;
    if (!_pImpl->_file) {
        return false;
    }
    _pImpl->_file.close();
    end();
    return true;
}

/*
  Read a line from the SD card
  Returns true if a line was read, even if it was empty.
  Returns false on EOF or error.  Errors display a message.
*/
Error SDCard::readFileLine(char* line, int maxlen) {
    if (!_pImpl->_file) {
        return Error::FsFailedRead;
    }

    _current_line_number += 1;
    int len = 0;
    while (_pImpl->_file.available()) {
        if (len >= maxlen) {
            return Error::LineLengthExceeded;
        }
        int c = _pImpl->_file.read();
        if (c < 0) {
            return Error::FsFailedRead;
        }
        if (c == '\n') {
            break;
        }
        line[len++] = c;
    }
    line[len] = '\0';
    return len || _pImpl->_file.available() ? Error::Ok : Error::Eof;
}

// return a percentage complete 50.5 = 50.5%
float SDCard::report_perc_complete() {
    if (!_pImpl->_file) {
        return 0.0;
    }
    return (float)_pImpl->_file.position() / (float)_pImpl->_file.size() * 100.0f;
}

uint32_t SDCard::lineNumber() {
    return _current_line_number;
}

// NotPresent can mean several different things:
// 1. The hardware does not support an SD card
// 2. The system configuration does not include the SD card
// 3. The SD card is not plugged in and there is a detect pin to tell us that
// 4. The SD card is not plugged in and we have to discover that by trying to read it.
// 5. The SD card is plugged in but its filesystem cannot be read
SDCard::State SDCard::test_or_open(bool refresh) {
    auto spiConfig = config->_spi;

    if (spiConfig == nullptr) {
        return SDCard::State::NotPresent;
    }

    auto csPin = spiConfig->_cs.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);

    //no need to go further if SD detect is not correct
    if (config->_sdCard->_cardDetect.defined() && !config->_sdCard->_cardDetect.read()) {
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

const char* SDCard::filename() {
    return _pImpl->_file ? _pImpl->_file.name() : "";
}

void SDCard::init() {
    static bool init_message = true;  // used to show messages only once.

    if (init_message) {
        _cardDetect.report("SD Card Detect");
        init_message = false;
    }

    _cardDetect.setAttr(Pin::Attr::Output);
}

SDCard::~SDCard() {
    delete _pImpl;
}
