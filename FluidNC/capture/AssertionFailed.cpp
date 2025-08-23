// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "AssertionFailed.h"

#include <cstdarg>
#include <cstring>

#include <stdexcept>

extern void DumpStackTrace(std::ostringstream& builder);

std::string stackTrace;

std::exception AssertionFailed::create(const char* condition, const char* msg, ...) {
    static char tmp[255];
    va_list     arg;
    va_start(arg, msg);
    vsnprintf(tmp, 255, msg, arg);
    va_end(arg);
    tmp[254] = 0;

#if 0
    //    msg = tmp;
    std::ostringstream oss;
    //    oss << "Error: ";

    oss << tmp;

    // oss << " at ";
    //    DumpStackTrace(oss);

    // Store in a static temp:
    static std::string info;
    info = oss.str();
#endif

#ifdef _MSC_VER
    throw std::exception(tmp);
#else
    throw std::runtime_error(tmp);
#endif
}
