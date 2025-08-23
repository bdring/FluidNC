// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <string>

#include <exception>

class AssertionFailed {
public:
    std::string stackTrace;
    std::string msg;

    static std::exception create(const char* condition) { return create(condition, "Assertion failed"); }
    static std::exception create(const char* condition, const char* msg, ...);

    const char* what() const { return msg.c_str(); }
};
