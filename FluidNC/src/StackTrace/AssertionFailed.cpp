// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "AssertionFailed.h"

#include <cstdarg>
#include <cstring>

#ifdef ESP32

#    ifdef BACKTRACE_ON_ASSERT
#        include "esp_debug_helpers.h"
#    endif
#    include "WString.h"
#    include "stdio.h"

AssertionFailed AssertionFailed::create(const char* condition, const char* msg, ...) {
    String st = condition;
    st += ": ";

    char    tmp[255];
    va_list arg;
    va_start(arg, msg);
    size_t len = vsnprintf(tmp, 255, msg, arg);
    va_end(arg);
    tmp[254] = 0;

    st += tmp;

#    ifdef BACKTRACE_ON_ASSERT  // Backtraces are usually hard to decode and thus confusing
    st += " at: ";
    st += esp_backtrace_print(10);
#    endif

    return AssertionFailed(st, tmp);
}

#else

#    include <iostream>
#    include <string>
#    include <sstream>
#    include "WString.h"

extern void DumpStackTrace(std::ostringstream& builder);

String stackTrace;

std::exception AssertionFailed::create(const char* condition, const char* msg, ...) {
    std::ostringstream oss;
    oss << std::endl;
    oss << "Error: " << std::endl;

    char    tmp[255];
    va_list arg;
    va_start(arg, msg);
    size_t len = vsnprintf(tmp, 255, msg, arg);
    tmp[254]   = 0;
    msg        = tmp;
    oss << tmp;

    oss << " at ";
    DumpStackTrace(oss);

    // Store in a static temp:
    static std::string info;
    info = oss.str();

#    ifdef _MSC_VER
    throw std::exception(info.c_str());
#    else
    throw std::exception();
#    endif
}

#endif
