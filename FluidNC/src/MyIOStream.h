// Copyright 2021 Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <Print.h>
#include <IPAddress.h>
#include <string>
#include <string_view>
#include <type_traits>

std::string IP_string(uint32_t ipaddr);

inline Print& operator<<(Print& lhs, char c) {
    lhs.print(c);
    return lhs;
}

inline Print& operator<<(Print& lhs, const char* v) {
    lhs.print(v);
    return lhs;
}

inline Print& operator<<(Print& lhs, const std::string_view& v) {
    lhs.write(reinterpret_cast<const uint8_t*>(v.data()), v.length());
    return lhs;
}

inline Print& operator<<(Print& lhs, const std::string& v) {
    lhs.print(v.c_str());
    return lhs;
}

// This handles all types that are forms of integers
template <typename Integer, std::enable_if_t<std::is_integral<Integer>::value, bool> = true>
inline Print& operator<<(Print& lhs, Integer v) {
    lhs.print(v);
    return lhs;
}

// This handles enums
template <typename Enum, std::enable_if_t<std::is_enum<Enum>::value, bool> = true>
inline Print& operator<<(Print& lhs, Enum v) {
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

inline Print& operator<<(Print& lhs, IPAddress v) {
    lhs.print(IP_string(v).c_str());
    return lhs;
}

class setprecision {
    uint8_t precision;

public:
    explicit setprecision(uint8_t p) : precision(p) {}

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
