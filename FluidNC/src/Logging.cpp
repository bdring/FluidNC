// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Logging.h"
#include "SettingsDefinitions.h"

#ifndef ESP32

#    include <iostream>

bool atMsgLevel(MsgLevel level) {
    return message_level == nullptr || message_level->get() >= level;
}

DebugStream::DebugStream(const char* name) {
    std::cout << "[MSG:" << name << ": ";
}
void DebugStream::add(char c) {
    std::cout << c;
}

DebugStream::~DebugStream() {
    std::cout << ']' << std::endl;
}

#else

bool atMsgLevel(MsgLevel level) {
    return message_level == nullptr || message_level->get() >= level;
}

#    include "Serial.h"
#    define LOG_CLIENT CLIENT_ALL

DebugStream::DebugStream(const char* name) {
    client_write(LOG_CLIENT, "[MSG:");
    client_write(LOG_CLIENT, name);
    client_write(LOG_CLIENT, ": ");
}

void DebugStream::add(char c) {
    char txt[2];
    txt[0] = c;
    txt[1] = '\0';
    client_write(LOG_CLIENT, txt);
}

DebugStream::~DebugStream() {
    client_write(LOG_CLIENT, "]");
    client_write(LOG_CLIENT, "\r\n");
}

#endif
