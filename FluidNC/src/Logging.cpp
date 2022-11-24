// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Logging.h"
#include "SettingsDefinitions.h"

#ifndef ESP32

#    include <iostream>

#    define DEBUG_OUT std::cout
bool atMsgLevel(MsgLevel level) {
    return message_level == nullptr || message_level->get() >= level;
}
#else
#    define DEBUG_OUT allChannels
bool atMsgLevel(MsgLevel level) {
    return message_level == nullptr || message_level->get() >= level;
}
#endif

DebugStream::DebugStream(const char* name) {
    _charcnt = 0;
    print("[MSG:");
    print(name);
    print(": ");
}

size_t DebugStream::write(uint8_t c) {
    if (_charcnt < MAXLINE - 2) {
        // Leave room for ]\n\0
        _outline[_charcnt++] = (char)c;
        return 1;
    }
    return 0;
}

DebugStream::~DebugStream() {
    // write() leaves space for three characters at the end
    _outline[_charcnt++] = ']';
    _outline[_charcnt++] = '\n';
    _outline[_charcnt++] = '\0';
    DEBUG_OUT << _outline;
}
