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

#pragma once

#include <cstdint>

enum MsgLevel {
    MsgLevelNone    = 0,
    MsgLevelError   = 1,
    MsgLevelWarning = 2,
    MsgLevelInfo    = 3,
    MsgLevelDebug   = 4,
    MsgLevelVerbose = 5,
};

#include "SimpleOutputStream.h"

// How to use logging? Well, the basics are pretty simple:
//
// - The syntax is like standard iostream's.
// - It is simplified though, so no ios or iomanip. But should be sufficient.
// - But, you wrap it in an 'info', 'debug', 'warn', 'error' or 'fatal'.
//
// The streams here ensure the data goes where it belongs, without too much
// buffer space being wasted.
//
// Example:
//
// log_info("Twelve is written as " << 12 << ", isn't it");

class DebugStream : public SimpleOutputStream {
public:
    DebugStream(const char* name);
    void add(char c) override;
    ~DebugStream();
};

#include "StringStream.h"

extern bool atMsgLevel(MsgLevel level);

// Note: these '{'..'}' scopes are here for a reason: the destructor should flush.
#define log_debug(x)                                                                                                                       \
    if (atMsgLevel(MsgLevelDebug)) {                                                                                                       \
        DebugStream ss("DBG");                                                                                                             \
        ss << x;                                                                                                                           \
    }

#define log_info(x)                                                                                                                        \
    if (atMsgLevel(MsgLevelInfo)) {                                                                                                        \
        DebugStream ss("INFO");                                                                                                            \
        ss << x;                                                                                                                           \
    }

#define log_warn(x)                                                                                                                        \
    if (atMsgLevel(MsgLevelWarning)) {                                                                                                     \
        DebugStream ss("WARN");                                                                                                            \
        ss << x;                                                                                                                           \
    }

#define log_error(x)                                                                                                                       \
    if (atMsgLevel(MsgLevelError)) {                                                                                                       \
        DebugStream ss("ERR");                                                                                                             \
        ss << x;                                                                                                                           \
    }

#define log_fatal(x)                                                                                                                       \
    {                                                                                                                                      \
        DebugStream ss("FATAL");                                                                                                           \
        ss << x;                                                                                                                           \
        Assert(false, "A fatal error occurred.");                                                                                          \
    }
