// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

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

#include "MyIOStream.h"

class DebugStream : public Print {
public:
    DebugStream(const char* name);
    size_t write(uint8_t c) override;
    ~DebugStream();
};

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
