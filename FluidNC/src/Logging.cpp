// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#include "Protocol.h"
#include "Serial.h"
#include "SettingsDefinitions.h"

EnumItem messageLevels2[] = { { MsgLevelNone, "None" }, { MsgLevelError, "Error" }, { MsgLevelWarning, "Warn" },
                              { MsgLevelInfo, "Info" }, { MsgLevelDebug, "Debug" }, { MsgLevelVerbose, "Verbose" },
                              EnumItem(MsgLevelNone) };

bool atMsgLevel(MsgLevel level) {
    return message_level == nullptr || message_level->get() >= level;
}

LogStream::LogStream(Channel& channel, MsgLevel level, const char* name, bool synchronous) :
    _channel(channel), _level(level), _synchronous(synchronous) {
    _line = new std::string();
    print(name);
}

LogStream::LogStream(Channel& channel, const char* name, bool synchronous) : LogStream(channel, MsgLevelNone, name, synchronous) {}
LogStream::LogStream(MsgLevel level, const char* name, bool synchronous) : LogStream(allChannels, level, name, synchronous) {}

size_t LogStream::write(uint8_t c) {
    *_line += (char)c;
    return 1;
}

LogStream::~LogStream() {
    if ((*_line).length() && (*_line)[0] == '[') {
        *_line += ']';
    }
    if (_synchronous) {
        _channel.print_msg(_level, _line->c_str());
    } else {
        send_line(_channel, _level, _line);
    }
}
