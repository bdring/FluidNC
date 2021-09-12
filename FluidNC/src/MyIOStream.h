// Copyright 2021 Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#include <Print.h>
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

inline Print& operator<<(Print& lhs, int v) {
    lhs.print(v);
    return lhs;
}

inline Print& operator<<(Print& lhs, unsigned int v) {
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
