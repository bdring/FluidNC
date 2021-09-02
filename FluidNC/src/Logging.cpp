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
#    define DEBUG_OUT allClients
bool atMsgLevel(MsgLevel level) {
    return message_level == nullptr || message_level->get() >= level;
}
#endif

DebugStream::DebugStream(const char* name) {
    DEBUG_OUT << "[MSG:" << name << ": ";
}

void DebugStream::add(char c) {
    DEBUG_OUT << c;
}

DebugStream::~DebugStream() {
    DEBUG_OUT << "]\n";
}
