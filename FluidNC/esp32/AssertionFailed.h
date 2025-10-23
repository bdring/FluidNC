// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <string>
#include <exception>

class AssertionFailed {
public:
    std::string msg;

    AssertionFailed(const std::string& st, const std::string& message) : msg(message) {}

    static std::exception create(const char* condition) { return create(condition, "Assertion failed"); }
    static std::exception create(const char* condition, const char* msg, ...);
};
