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

#include "WString.h"

#ifdef ESP32
class AssertionFailed {
public:
    String stackTrace;
    String msg;

    AssertionFailed(String st, String message) : stackTrace(st), msg(message) {}

    static AssertionFailed create(const char* condition) { return create(condition, "Assertion failed"); }
    static AssertionFailed create(const char* condition, const char* msg, ...);

    const char* what() const { return msg.c_str(); }
};

#else
#    include <exception>

class AssertionFailed {
public:
    String stackTrace;
    String msg;

    static std::exception create(const char* condition) { return create(condition, "Assertion failed"); }
    static std::exception create(const char* condition, const char* msg, ...);

    const char* what() const { return msg.c_str(); }
};

#endif
