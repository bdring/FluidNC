// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "src/StartupLog.h"
#include "src/Protocol.h"  // send_line()
#include <sstream>

// The startup log is stored in RTC RAM that is preserved across
// resets.  That lets us show the previous startup log if the
// system panics and resets.

// The size is limited by the size of RTC RAM minus system usage thereof
static const size_t           _maxlen = 7000;
static RTC_NOINIT_ATTR char   _messages[_maxlen];
static RTC_NOINIT_ATTR size_t _len;
static bool                   _paniced;

void StartupLog::init() {
    if (esp_reset_reason() == ESP_RST_PANIC) {
        _paniced = true;
    } else {
        _paniced = false;
        _len     = 0;
    }
}
size_t StartupLog::write(uint8_t data) {
    if (_paniced || _len >= _maxlen) {
        return 0;
    }
    _messages[_len++] = (char)data;
    return 1;
}
void StartupLog::dump(Channel& out) {
    if (_paniced) {
        log_error_to(out, "Showing startup log from previous panic");
    }
    for (size_t i = 0; i < _len;) {
        std::string line;
        while (i < _len) {
            char c = _messages[i++];
            if (c == '\n') {
                break;
            }
            line += c;
        }
        log_to(out, line);
    }
}

StartupLog::~StartupLog() {}

StartupLog startupLog;
