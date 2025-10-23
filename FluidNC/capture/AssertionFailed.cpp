// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "AssertionFailed.h"

#include <cstdarg>
#include <cstring>

#include <stdexcept>

std::exception AssertionFailed::create(const char* condition, const char* msg, ...) {
    std::string st = condition;
    st += ": ";

    char    tmp[255];
    va_list arg;
    va_start(arg, msg);
    vsnprintf(tmp, 255, msg, arg);
    va_end(arg);
    tmp[254] = 0;

    st += tmp;

#ifdef _MSC_VER
    return std::exception(tmp);
#else
    return std::runtime_error(tmp);
#endif
}
