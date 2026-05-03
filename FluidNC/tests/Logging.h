// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstdint>
// Stub implmentation of log_* for use with unit tests

#include <string>
#include <string_view>

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

class LogStream {
public:
    LogStream(MsgLevel level, const char* name) {}
    LogStream(Channel& channel, MsgLevel level) {}
    LogStream(Channel& channel, const char* name) {}
    LogStream(Channel& channel, MsgLevel level, const char* name) {}

    template <typename T>
    LogStream& operator<<(const T&) {
        return *this;
    }
};

inline bool atMsgLevel(MsgLevel level) {
    return false;
}

// clang-format off

// Note: these '{'..'}' scopes are here for a reason: the destructor should flush.

// #define log_bare(prefix, x) { LogStream ss(prefix); ss << x; }
#define log_msg(x) do { } while (0)
#define log_verbose(x) do { } while (0)
#define log_debug(x) do { } while (0)
#define log_info(x) do { } while (0)
#define log_warn(x) do { } while (0)
#define log_error(x) do { } while (0)
#define log_config_error(x) do { } while (0)
#define log_fatal(x) do { } while (0)

#define log_msg_to(out, x) do { } while (0)
#define log_verbose_to(out, x) do { } while (0)
#define log_debug_to(out, x) do { } while (0)
#define log_info_to(out, x) do { } while (0)
#define log_warn_to(out, x) do { } while (0)
#define log_error_to(out, x) do { } while (0)
#define log_fatal_to(out, x) do { } while (0)

#define log_stream(out, x) do { } while (0)
#define log_string(out, x) do { } while (0)
