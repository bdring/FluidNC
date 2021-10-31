// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "InputFile.h"

#include "Report.h"
#include "Logging.h"

#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>

class InputFile::FileWrap {
public:
    FileWrap() : _file(nullptr) {}
    File _file;
};

InputFile::InputFile(fs::FS& fs, const char* path, Channel& channel, WebUI::AuthenticationLevel auth_level) :
    _pImpl(new FileWrap()), _channel(channel), _auth_level(auth_level), _line_num(0) {
    //    if (fs == SD) {
    SD.begin();
    //    }
    log_info("input file " << path);
    _pImpl->_file = fs.open(path);
    if (!_pImpl->_file) {
        log_info("fail");
        _pImpl->_file = fs.open("/test.nc");
        if (!_pImpl->_file) {
            log_info("FAIL");
            //        if (fs == SD) {
            //            SD.end();
            //        }
            throw Error::FsFailedOpenFile;
        }
    }
}
/*
  Read a line from the file
  Returns Error::Ok if a line was read, even if the line was empty.
  Returns Error::EOF on end of file.
  Returns other Error code on error, after displaying a message.
*/
Error InputFile::readLine(char* line, int maxlen) {
    if (!_pImpl->_file) {
        return Error::FsFailedRead;
    }

    ++_line_num;
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
float InputFile::percent_complete() {
    if (!_pImpl->_file) {
        return 0.0;
    }
    return (float)_pImpl->_file.position() / (float)_pImpl->_file.size() * 100.0f;
}

const char* InputFile::filename() {
    return _pImpl->_file ? _pImpl->_file.name() : "";
}

InputFile::~InputFile() {
    _pImpl->_file.close();
    delete _pImpl;
    SD.end();
}

InputFile* infile = nullptr;
