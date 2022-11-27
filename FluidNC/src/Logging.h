// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstdint>

class Channel;

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
    DebugStream(const char* name, Channel* channel);
    DebugStream(const char* name);
    size_t write(uint8_t c) override;
    ~DebugStream();

private:
    // Log lines are collected in a buffer and sent to the output stream
    // when the line is complete, thus avoiding the possibility of interleaving
    // output from multiple cores.
    int _charcnt = 0;

    Channel* _channel;
    char     _line[MAX_MESSAGE_LINE];
};

extern bool atMsgLevel(MsgLevel level);

// clang-format off

// Note: these '{'..'}' scopes are here for a reason: the destructor should flush.

#define log_bare(prefix, x) { DebugStream ss(prefix); ss << x; }
#define log_msg(x) { DebugStream ss("MSG: "); ss << x; }
#define log_verbose(x) if (atMsgLevel(MsgLevelVerbose)) { DebugStream ss("MSG:VRB: "); ss << x; }
#define log_debug(x) if (atMsgLevel(MsgLevelDebug)) { DebugStream ss("MSG:DBG: "); ss << x; }
#define log_info(x) if (atMsgLevel(MsgLevelInfo)) { DebugStream ss("MSG:INFO: "); ss << x; }
#define log_warn(x) if (atMsgLevel(MsgLevelWarning)) { DebugStream ss("MSG:WARN: "); ss << x; }
#define log_error(x) if (atMsgLevel(MsgLevelError)) { DebugStream ss("MSG:ERR: "); ss << x; }
#define log_fatal(x) { DebugStream ss("MSG:FATAL: "); ss << x;  Assert(false, "A fatal error occurred."); }

#define log_bare_to(out, prefix, x) { DebugStream ss(prefix, &out); ss << x; }
#define log_msg_to(out, x) { DebugStream ss("MSG: ", &out); ss << x; }
#define log_verbose_to(out, x) if (atMsgLevel(MsgLevelVerbose)) { DebugStream ss("MSG:VRB: ", &out); ss << x; }
#define log_debug_to(out, x) if (atMsgLevel(MsgLevelDebug)) { DebugStream ss("MSG:DBG: ", &out); ss << x; }
#define log_info_to(out, x) if (atMsgLevel(MsgLevelInfo)) { DebugStream ss("MSG:INFO: ", &out); ss << x; }
#define log_warn_to(out, x) if (atMsgLevel(MsgLevelWarning)) { DebugStream ss("MSG:WARN: ", &out); ss << x; }
#define log_error_to(out, x) if (atMsgLevel(MsgLevelError)) { DebugStream ss("MSG:ERR: ", &out); ss << x; }
#define log_fatal_to(out, x) { DebugStream ss("MSG:FATAL: ", &out); ss << x;  Assert(false, "A fatal error occurred."); }

