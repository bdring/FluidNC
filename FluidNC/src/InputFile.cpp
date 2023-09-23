// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "InputFile.h"

#include "Report.h"

InputFile::InputFile(const char* defaultFs, const char* path, WebUI::AuthenticationLevel auth_level, Channel& out) :
    FileStream(path, "r", defaultFs), _auth_level(auth_level), _out(out), _line_num(0) {}
/*
  Read a line from the file
  Returns Error::Ok if a line was read, even if the line was empty.
  Returns Error::EOF on end of file.
  Returns other Error code on error, after displaying a message.
*/
Error InputFile::readLine(char* line, int maxlen) {
    ++_line_num;
    int len = 0;
    int c;
    while ((c = read()) >= 0) {
        if (len >= maxlen) {
            return Error::LineLengthExceeded;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            break;
        }
        line[len++] = c;
    }
    line[len] = '\0';
    return len || c >= 0 ? Error::Ok : Error::Eof;
}

// return a percentage complete 50.5 = 50.5%
float InputFile::percent_complete() {
    return (float)position() / (float)size() * 100.0f;
}

void InputFile::ack(Error status) {
    if (status != Error::Ok) {
        log_error(static_cast<int>(status) << " (" << errorString(status) << ") in " << path() << " at line " << getLineNumber());
        if (status != Error::GcodeUnsupportedCommand) {
            // Do not stop on unsupported commands because most senders do not
            // Stop the file job on other errors
            _notifyf("File job error", "Error:%d in %s at line: %d", status, path(), getLineNumber());
            allChannels.kill(this);
            return;
        }
    }
    _readyNext = true;
}

std::string InputFile::_progress = "";

#include <sstream>
#include <iomanip>

Channel* InputFile::pollLine(char* line) {
    // File input never returns realtime characters, so we do nothing
    // if line is null.
    if (!_readyNext || !line) {
        return nullptr;
    }
    switch (auto err = readLine(line, Channel::maxLine)) {
        case Error::Ok: {
            std::ostringstream s;
            s << "SD:" << std::fixed << std::setprecision(2) << percent_complete() << "," << path().c_str();
            _progress = s.str();
        }
            return &allChannels;
        case Error::Eof:
            _progress = "";
            _notifyf("File job done", "%s file job succeeded", path());
            log_msg(path() << " file job succeeded");
            allChannels.kill(this);
            return nullptr;
        default:
            _progress = "";
            log_error(static_cast<int>(err) << " (" << errorString(err) << ") in " << path() << " at line " << getLineNumber());
            allChannels.kill(this);
            return nullptr;
    }
}

void InputFile::stopJob() {
    //Report print stopped
    _notifyf("File print canceled", "Reset during file job at line: %d", getLineNumber());
    log_info("Reset during file job at line: " << getLineNumber());
    _progress = "";
    allChannels.kill(this);
}

InputFile::~InputFile() {
    _progress = "";
}
