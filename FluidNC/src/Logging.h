// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstdint>
#include "EnumItem.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class Channel;

enum MsgLevel {
    MsgLevelNone    = 0,
    MsgLevelError   = 1,
    MsgLevelWarning = 2,
    MsgLevelInfo    = 3,
    MsgLevelDebug   = 4,
    MsgLevelVerbose = 5,
};

struct LogMessage {
    Channel* channel;
    void*    line;
    MsgLevel level;
    bool     isString;
};

extern TaskHandle_t outputTask;

extern xQueueHandle message_queue;

extern EnumItem messageLevels2[];

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
    LogStream(Channel& channel, MsgLevel level);
    LogStream(Channel& channel, const char* name);
    LogStream(Channel& channel, MsgLevel level, const char* name);
    LogStream(MsgLevel level, const char* name);
    size_t write(uint8_t c) override;
    ~LogStream();

private:
    Channel&     _channel;
    std::string* _line;
    MsgLevel     _level;
};

extern bool atMsgLevel(MsgLevel level);

// clang-format off

// Note: these '{'..'}' scopes are here for a reason: the destructor should flush.

// #define log_bare(prefix, x) { LogStream ss(prefix); ss << x; }
#define log_msg(x) { LogStream ss(MsgLevelNone, "[MSG:"); ss << x; }
#define log_verbose(x) if (atMsgLevel(MsgLevelVerbose)) { LogStream ss(MsgLevelVerbose, "[MSG:VRB: "); ss << x; }
#define log_debug(x) if (atMsgLevel(MsgLevelDebug)) { LogStream ss(MsgLevelDebug, "[MSG:DBG: "); ss << x; }
#define log_info(x) if (atMsgLevel(MsgLevelInfo)) { LogStream ss(MsgLevelInfo, "[MSG:INFO: "); ss << x; }
#define log_warn(x) if (atMsgLevel(MsgLevelWarning)) { LogStream ss(MsgLevelWarning, "[MSG:WARN: "); ss << x; }
#define log_error(x) if (atMsgLevel(MsgLevelError)) { LogStream ss(MsgLevelError, "[MSG:ERR: "); ss << x; }
#define log_fatal(x) { LogStream ss(MsgLevelNone, "[MSG:FATAL: "); ss << x;  Assert(false, "A fatal error occurred."); }

#define log_msg_to(out, x) { LogStream ss(out, MsgLevelNone, "[MSG:"); ss << x; }
#define log_verbose_to(out, x) if (atMsgLevel(MsgLevelVerbose)) { LogStream ss(out, MsgLevelVerbose, "[MSG:VRB: "); ss << x; }
#define log_debug_to(out, x) if (atMsgLevel(MsgLevelDebug)) { LogStream ss(out, MsgLevelDebug, "[MSG:DBG: "); ss << x; }
#define log_info_to(out, x) if (atMsgLevel(MsgLevelInfo)) { LogStream ss(out, MsgLevelInfo, "[MSG:INFO: "); ss << x; }
#define log_warn_to(out, x) if (atMsgLevel(MsgLevelWarning)) { LogStream ss(out, MsgLevelWarning, "[MSG:WARN: "); ss << x; }
#define log_error_to(out, x) if (atMsgLevel(MsgLevelError)) { LogStream ss(out, MsgLevelError, "[MSG:ERR: "); ss << x; }
#define log_fatal_to(out, x) { LogStream ss(out, MsgLevelNone, "[MSG:FATAL: "); ss << x;  Assert(false, "A fatal error occurred."); }

// #define log_to(out, prefix, x) { LogStream ss(out, MsgLevelNone, prefix); ss << x; }
#define log_stream(out, x) { LogStream ss(out, MsgLevelNone); ss << x; }
#define log_string(out, x) out.sendLine(MsgLevelNone, x)
