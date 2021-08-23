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

#include "StringRange.h"
#include "Pin.h"

#include <cstring>

class SimpleOutputStream {
    static char* intToBuf(int value, char* dst);
    static char* uintToBuf(unsigned int value, char* dst);

public:
    SimpleOutputStream() = default;

    SimpleOutputStream(const SimpleOutputStream& o) = delete;
    SimpleOutputStream(SimpleOutputStream&& o)      = delete;

    SimpleOutputStream& operator=(const SimpleOutputStream& o) = delete;
    SimpleOutputStream& operator=(SimpleOutputStream&& o) = delete;

    virtual void add(char c) = 0;
    virtual void flush() {}

    void add(const char* s);
    void add(int value);
    void add(unsigned int value);
    void add(float value, int numberDigits, int precision);
    void add(StringRange range);
    void add(const Pin& pin);

    virtual ~SimpleOutputStream() {}
};

inline SimpleOutputStream& operator<<(SimpleOutputStream& lhs, char c) {
    lhs.add(c);
    return lhs;
}

inline SimpleOutputStream& operator<<(SimpleOutputStream& lhs, const char* v) {
    lhs.add(v);
    return lhs;
}

inline SimpleOutputStream& operator<<(SimpleOutputStream& lhs, int v) {
    lhs.add(v);
    return lhs;
}

inline SimpleOutputStream& operator<<(SimpleOutputStream& lhs, unsigned int v) {
    lhs.add(v);
    return lhs;
}

inline SimpleOutputStream& operator<<(SimpleOutputStream& lhs, float v) {
    lhs.add(v, 4, 3);
    return lhs;
}

inline SimpleOutputStream& operator<<(SimpleOutputStream& lhs, StringRange v) {
    lhs.add(v);
    return lhs;
}

inline SimpleOutputStream& operator<<(SimpleOutputStream& lhs, const Pin& v) {
    lhs.add(v);
    return lhs;
}
