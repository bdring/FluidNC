/*
    Part of Grbl_ESP32
    2021 -  Stefan de Bruijn

    Grbl_ESP32 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Grbl_ESP32 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Grbl_ESP32.  If not, see <http://www.gnu.org/licenses/>.
*/

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
