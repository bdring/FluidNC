// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "InputFile.h"

#include "Report.h"

InputFile::InputFile(const char* defaultFs, const char* path) : FileStream(path, "r", defaultFs) {}
/*
  Read a line from the file
  Returns Error::Ok if a line was read, even if the line was empty.
  Returns Error::EOF on end of file.
  Returns other Error code on error, after displaying a message.
*/
Error InputFile::readLine(char* line, int maxlen) {
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
            ++_line_number;
            break;
        }
        line[len++] = c;
    }
    line[len] = '\0';
    return len || c >= 0 ? Error::Ok : Error::Eof;
}

void InputFile::ack(Error status) {
    if (status != Error::Ok) {
        log_error(static_cast<int>(status) << " (" << errorString(status) << ") in " << name() << " at line " << lineNumber());
        if (status != Error::GcodeUnsupportedCommand) {
            // Do not stop on unsupported commands because most senders do not stop.
            // Stop the file job on other errors
            _notifyf("File job error", "Error:%d in %s at line: %d", status, name(), lineNumber());
            _pending_error == status;
        }
    }
}

#include <sstream>
#include <iomanip>

Error InputFile::pollLine(char* line) {
    // File input never returns realtime characters, so we do nothing
    // if line is null.
    if (!line) {
        return Error::NoData;
    }
    if (_pending_error != Error::Ok) {
        return _pending_error;
    }
    switch (auto err = readLine(line, Channel::maxLine)) {
        case Error::Ok: {
            float percent_complete = ((float)position()) * 100.0f / size();

            std::ostringstream s;
            s << "SD:" << std::fixed << std::setprecision(2) << percent_complete << "," << path().c_str();
            _progress = s.str();
        }
            return Error::Ok;
        case Error::Eof:
            _progress = "SD: ";
            _progress += name();
            _progress += ": Sent";
            return Error::Eof;
        default:
            _progress = "";
            return err;
    }
}

InputFile::~InputFile() {}
