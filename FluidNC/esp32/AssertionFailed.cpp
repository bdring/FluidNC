// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "AssertionFailed.h"

#include <cstdarg>
#include <cstring>

#ifdef BACKTRACE_ON_ASSERT
#    include "esp_debug_helpers.h"
#endif
#include "stdio.h"

AssertionFailed AssertionFailed::create(const char* condition, const char* msg, ...) {
    std::string st = condition;
    st += ": ";

    char    tmp[255];
    va_list arg;
    va_start(arg, msg);
    vsnprintf(tmp, 255, msg, arg);
    va_end(arg);
    tmp[254] = 0;

    st += tmp;

#ifdef BACKTRACE_ON_ASSERT  // Backtraces are usually hard to decode and thus confusing
    st += " at: ";
    st += esp_backtrace_print(10);
#endif

    return AssertionFailed(st, tmp);
}
