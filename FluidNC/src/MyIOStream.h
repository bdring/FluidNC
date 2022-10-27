// Copyright 2021 Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <Print.h>
#include <IPAddress.h>
#include <string>

#include "Pin.h"

inline Print& operator<<(Print& lhs, char c) {
    lhs.print(c);
    return lhs;
}

inline Print& operator<<(Print& lhs, const char* v) {
    lhs.print(v);
    return lhs;
}

inline Print& operator<<(Print& lhs, String v) {
    lhs.print(v.c_str());
    return lhs;
}

inline Print& operator<<(Print& lhs, std::string v) {
    lhs.print(v.c_str());
    return lhs;
}

inline Print& operator<<(Print& lhs, int v) {
    lhs.print(v);
    return lhs;
}

inline Print& operator<<(Print& lhs, unsigned int v) {
    lhs.print(v);
    return lhs;
}

inline Print& operator<<(Print& lhs, uint64_t v) {
    lhs.print(v);
    return lhs;
}

inline Print& operator<<(Print& lhs, float v) {
    lhs.print(v, 3);
    return lhs;
}

inline Print& operator<<(Print& lhs, double v) {
    lhs.print(v, 3);
    return lhs;
}

inline Print& operator<<(Print& lhs, const Pin& v) {
    lhs.print(v.name());
    return lhs;
}

inline Print& operator<<(Print& lhs, IPAddress a) {
    lhs.print(a.toString());
    return lhs;
}

class setprecision {
    int precision;

public:
    setprecision(int p) : precision(p) {}

    inline void Write(Print& stream, float f) const { stream.print(f, precision); }
    inline void Write(Print& stream, double d) const { stream.print(d, precision); }
};

template <class T>
class FormatContainer {
public:
    Print& stream;
    T      formatter;

    FormatContainer(Print& l, const T& fmt) : stream(l), formatter(fmt) {}
};

inline FormatContainer<setprecision> operator<<(Print& lhs, setprecision rhs) {
    return FormatContainer<setprecision>(lhs, rhs);
}
template <class T, typename U>
inline Print& operator<<(FormatContainer<T> lhs, U f) {
    lhs.formatter.Write(lhs.stream, f);
    return lhs.stream;
}
