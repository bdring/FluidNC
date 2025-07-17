// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <string>

class AssertionFailed {
public:
    std::string stackTrace;
    std::string msg;

#ifdef ESP32
    AssertionFailed(const std::string& st, const std::string& message) : stackTrace(st), msg(message) {}
#else
    // Otherwise AssertionFailed::create throws a std::exception
#endif

    static AssertionFailed create(const char* condition) {
        return create(condition, "Assertion failed");
    }
    static AssertionFailed create(const char* condition, const char* msg, ...);

    const char* what() const {
        return msg.c_str();
    }
};
