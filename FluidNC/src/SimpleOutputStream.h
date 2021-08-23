// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

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
