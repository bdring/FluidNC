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

#include "AssertionFailed.h"

#include <cstdarg>
#include <cstring>

#ifdef ESP32

#    include "debug_helpers.h"
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

    st += " at: ";
    st += esp_backtrace_print(10);

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
    throw std::exception(info.c_str());
}

#endif
