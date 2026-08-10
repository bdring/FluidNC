// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "StartupLog.h"
#include "Protocol.h"  // send_line()
#include <sstream>

// The size is limited to mimic the size of ESP32 RTC RAM minus system usage thereof
static const size_t _maxlen = 7000;
static char         _messages[_maxlen];
static size_t       _len = 0;

StartupLog::StartupLog() : Channel("Startup Log") {}

size_t StartupLog::write(uint8_t data) {
    if (_len >= _maxlen) {
        return 0;
    }
    _messages[_len++] = (char)data;
    return 1;
}

// cppcheck-suppress unusedFunction
void StartupLog::dump(Channel& out) {
    for (size_t i = 0; i < _len;) {
        std::string line;
        while (i < _len) {
            char c = _messages[i++];
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                break;
            }
            line += c;
        }
        if (!line.empty() && line.back() == ']') {
            line.pop_back();
        }
        log_stream(out, line);
    }
}

StartupLog::~StartupLog() {}

StartupLog startupLog;
