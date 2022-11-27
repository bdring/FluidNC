// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#include "Protocol.h"
#include "Serial.h"
#include "SettingsDefinitions.h"

bool atMsgLevel(MsgLevel level) {
    return message_level == nullptr || message_level->get() >= level;
}

DebugStream::DebugStream(const char* name, Channel* channel) {
    _channel = channel;
    _charcnt = 0;
    print("[");
    print(name);
}
DebugStream::DebugStream(const char* name) : DebugStream(name, &allChannels) {}

size_t DebugStream::write(uint8_t c) {
    if (_charcnt < MAX_MESSAGE_LINE - 2) {
        // Leave room for ]\0
        _line[_charcnt++] = (char)c;
        return 1;
    }
    return 0;
}

DebugStream::~DebugStream() {
    // write() leaves space for the null at the end
    _line[_charcnt++] = ']';
    _line[_charcnt++] = '\0';
    send_line(_channel, _line);
}
