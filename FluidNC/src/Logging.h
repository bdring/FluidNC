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

class LogStream : public Print {
public:
    LogStream(Print& channel, const char* name);
    LogStream(const char* name);
    size_t write(uint8_t c) override;
    ~LogStream();

private:
    Print&       _channel;
    std::string* _line;
};

extern bool atMsgLevel(MsgLevel level);

// clang-format off

// Note: these '{'..'}' scopes are here for a reason: the destructor should flush.

//#define log_bare(prefix, x) { LogStream ss(prefix); ss << x; }
#define log_data(x) { LogStream ss(""); ss << x; }
#define log_msg(x) { LogStream ss("[MSG: "); ss << x; }
#define log_verbose(x) if (atMsgLevel(MsgLevelVerbose)) { LogStream ss("[MSG:VRB: "); ss << x; }
#define log_debug(x) if (atMsgLevel(MsgLevelDebug)) { LogStream ss("[MSG:DBG: "); ss << x; }
#define log_info(x) if (atMsgLevel(MsgLevelInfo)) { LogStream ss("[MSG:INFO: "); ss << x; }
#define log_warn(x) if (atMsgLevel(MsgLevelWarning)) { LogStream ss("[MSG:WARN: "); ss << x; }
#define log_error(x) if (atMsgLevel(MsgLevelError)) { LogStream ss("[MSG:ERR: "); ss << x; }
#define log_fatal(x) { LogStream ss("[MSG:FATAL: "); ss << x;  Assert(false, "A fatal error occurred."); }

#define log_msg_to(out, x) { LogStream ss(out, "[MSG: "); ss << x; }
#define log_verbose_to(out, x) if (atMsgLevel(MsgLevelVerbose)) { LogStream ss(out, "[MSG:VRB: "); ss << x; }
#define log_debug_to(out, x) if (atMsgLevel(MsgLevelDebug)) { LogStream ss(out, "[MSG:DBG: "); ss << x; }
#define log_info_to(out, x) if (atMsgLevel(MsgLevelInfo)) { LogStream ss(out, "[MSG:INFO: "); ss << x; }
#define log_warn_to(out, x) if (atMsgLevel(MsgLevelWarning)) { LogStream ss(out, "[MSG:WARN: "); ss << x; }
#define log_error_to(out, x) if (atMsgLevel(MsgLevelError)) { LogStream ss(out, "[MSG:ERR: "); ss << x; }
#define log_fatal_to(out, x) { LogStream ss(out, "[MSG:FATAL: "); ss << x;  Assert(false, "A fatal error occurred."); }

// GET_MACRO is a preprocessor trick to let log_to() behave differently
// with 2 arguments vs 3.  The 2 argument case is super efficient
// while the 3 argument case is slightly less so, but you get to contruct
// the message string with << stream operators, while 2 arguments can
// only send a fixed string.  The fixed-string case is especially important
// because it is how the "ok" ack - the most common message - is sent.

#define GET_MACRO(_1,_2,_3, NAME, ...) NAME
#define log_to(...) GET_MACRO(__VA_ARGS__, log_to3, log_to2)(__VA_ARGS__)

#define log_to2(out, prefix) send_line(out, prefix)
#define log_to3(out, prefix, x) { LogStream ss(out, prefix); ss << x; }
