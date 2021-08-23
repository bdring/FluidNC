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

#include <cstring>

#include "WString.h"

class StringRange {
    const char* start_;
    const char* end_;

public:
    StringRange() : start_(nullptr), end_(nullptr) {}

    StringRange(const char* str) : start_(str), end_(str + strlen(str)) {}

    StringRange(const char* start, const char* end) : start_(start), end_(end) {}

    StringRange(const StringRange& o) = default;
    StringRange(StringRange&& o)      = default;

    StringRange(const String& str) : StringRange(str.begin(), str.end()) {}

    StringRange& operator=(const StringRange& o) = default;
    StringRange& operator=(StringRange&& o) = default;

    int find(char c) const {
        const char* s = start_;
        for (; s != end_ && *s != c; ++s) {}
        return (*s) ? (s - start_) : -1;
    }

    StringRange subString(int index, int length) const {
        const char* s = start_ + index;
        if (s > end_) {
            s = end_;
        }
        const char* e = s + length;
        if (e > end_) {
            e = end_;
        }
        return StringRange(s, e);
    }

    // Blank-delimited word
    StringRange nextWord() {
        while (start_ != end_ && *start_ == ' ') {
            ++start_;
        }
        const char* s = start_;
        while (start_ != end_ && *start_ != ' ') {
            ++start_;
        }
        return StringRange(s, start_);
    }

    // Character-delimited word
    StringRange nextWord(char c) {
        const char* s = start_;
        // Scan to delimiter or end of string
        while (start_ != end_ && *start_ != c) {
            ++start_;
        }
        const char* e = start_;
        // Skip the delimiter if present
        if (start_ != end_) {
            ++start_;
        }
        return StringRange(s, e);
    }

    bool equals(const StringRange& o) const {
        auto l = length();
        return l == o.length() && !strncasecmp(start_, o.start_, l);
    }

    bool equals(const char* o) const {
        const char* c  = start_;
        const char* oc = o;
        for (; c != end_ && *oc != '\0' && tolower(*c) == tolower(*oc); ++c, ++oc) {}
        return c == end_ && *oc == '\0';
    }

    int length() const { return end_ - start_; }

    // Iterator support:
    const char* begin() const { return start_; }
    const char* end() const { return end_; }

    String str() const {
        // TODO: Check if we can eliminate this function. I'm pretty sure we can.
        auto len = length();
        if (len == 0) {
            return String();
        } else {
            char* buf = new char[len + 1];
            memcpy(buf, begin(), len);
            buf[len] = 0;
            String tmp(buf);
            delete[] buf;
            return tmp;
        }
    }

    inline bool isUInteger(uint32_t& intval) {
        char* intEnd;
        intval = strtol(start_, &intEnd, 10);
        return intEnd == end_;
    }

    inline bool isInteger(int32_t& intval) {
        char* intEnd;
        intval = strtol(start_, &intEnd, 10);
        return intEnd == end_;
    }

    inline bool isFloat(float& floatval) {
        char* floatEnd;
        floatval = float(strtod(start_, &floatEnd));
        return floatEnd == end_;
    }
};
