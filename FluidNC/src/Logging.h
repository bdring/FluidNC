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
    // Log lines are collected in a buffer and sent to the output stream
    // when the line is complete, thus avoiding the possibility of interleaving
    // output from multiple cores.
    static const int MAXLINE = 256;

    char _outline[MAXLINE];
    int  _charcnt = 0;

public:
    DebugStream(const char* name);
    size_t write(uint8_t c) override;
    ~DebugStream();
};

extern bool atMsgLevel(MsgLevel level);

// Note: these '{'..'}' scopes are here for a reason: the destructor should flush.
#define log_verbose(x)                                                                                                                     \
    if (atMsgLevel(MsgLevelVerbose)) {                                                                                                     \
        DebugStream ss("VRB");                                                                                                             \
        ss << x;                                                                                                                           \
    }

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

#define log_verbose_to(out, x)                                                                                                             \
    if (atMsgLevel(MsgLevelVerbose)) {                                                                                                     \
        out << "[MSG:VRB: ";                                                                                                               \
        out << x;                                                                                                                          \
        out << "]\n";                                                                                                                      \
    }

#define log_debug_to(out, x)                                                                                                               \
    if (atMsgLevel(MsgLevelDebug)) {                                                                                                       \
        out << "[MSG:DBG: ";                                                                                                               \
        out << x;                                                                                                                          \
        out << "]\n";                                                                                                                      \
    }

#define log_info_to(out, x)                                                                                                                \
    if (atMsgLevel(MsgLevelInfo)) {                                                                                                        \
        out << "[MSG:INFO: ";                                                                                                              \
        out << x;                                                                                                                          \
        out << "]\n";                                                                                                                      \
    }

#define log_warn_to(out, x)                                                                                                                \
    if (atMsgLevel(MsgLevelWarning)) {                                                                                                     \
        out << "[MSG:WARN: ";                                                                                                              \
        out << x;                                                                                                                          \
        out << "]\n";                                                                                                                      \
    }

#define log_error_to(out, x)                                                                                                               \
    if (atMsgLevel(MsgLevelError)) {                                                                                                       \
        out << "[MSG:ERR: ";                                                                                                               \
        out << x;                                                                                                                          \
        out << "]\n";                                                                                                                      \
    }

#define log_fatal_to(out, x)                                                                                                               \
    {                                                                                                                                      \
        out << "[MSG:FATAL: ";                                                                                                             \
        out << x;                                                                                                                          \
        out << "]\n";                                                                                                                      \
        Assert(false, "A fatal error occurred.");                                                                                          \
    }
