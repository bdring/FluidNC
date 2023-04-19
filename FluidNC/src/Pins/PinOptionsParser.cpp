// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "PinOptionsParser.h"

#include <cstring>
#include <cctype>
#include <cstdlib>

namespace Pins {
    PinOption::PinOption(const char* start, const char* end) : _start(start), _end(end), _key(start), _value(start) { tokenize(); }

    // Copy the value into a null-terminated string, converting to lower case
    const char* PinOption::value() const {
        static char str[100];

        int valuelen = _valueend - _value;
        if (valuelen > 100) {
            valuelen = 99;
        }
        const char* p = _value;
        int         i;
        for (i = 0; i < valuelen; i++) {
            str[i] = ::tolower(*p++);
        }
        str[i] = '\0';
        return str;
    }

    void PinOption::tokenize() {
        if (_start != _end) {
            _key = _start;

            auto i = _start;
            for (; i != _end && (*i) != ':' && (*i) != ';' && (*i) != '='; ++i) {}

            if (i == _end) {
                // [start, end> is a key; value is nul
                _value    = _end;
                _valueend = _end;
                _keyend   = _end;
                _start    = i;
            } else if (*i == '=') {
                // Parsing a key-value pair.
                //
                // Mark end of the key, which is now in [start, end>
                _keyend = i;
                ++i;

                _value = i;

                // Parse the value:
                for (; i != _end && (*i) != ':' && (*i) != ';'; ++i) {}

                _valueend = i;
                if (i != _end) {
                    _start = i + 1;
                } else {
                    _start = i;
                }
            } else {  // must be ':' or ';'
                      // [start, i> is a key; value is nul
                _keyend = _value = _valueend = i;
                _start                       = i + 1;
            }
        } else {
            // Both key and value are nul.
            _key = _value = _end;
            _keyend = _valueend = _end;
        }
    }

    bool PinOption::is(const char* option) const {
        const char* k = _key;
        while (*option && k != _keyend) {
            if (::tolower(*k++) != ::tolower(*option++)) {
                return false;
            }
        }
        // If we get here, we have reached the end of either option or key
        // and the initial substrings match ignoring case.
        // If we are at the end of both, we have a match
        return !*option && k == _keyend;
    }

    int PinOption::iValue() const {
        // Parse to integer
        return ::atoi(value());
    }

    double PinOption::dValue() const {
        // Parse to integer
        return ::atof(value());
    }

    PinOption& PinOption ::operator++() {
        tokenize();
        return *this;
    }

    PinOptionsParser::PinOptionsParser(const char* buffer, const char* endBuffer) : _buffer(buffer), _bufferEnd(endBuffer) {
        // trim whitespaces:
        while (buffer != endBuffer && ::isspace(*buffer)) {
            ++buffer;
        }
        if (buffer != endBuffer) {
            while (buffer - 1 != endBuffer && ::isspace(endBuffer[-1])) {
                --endBuffer;
            }
        }
        _buffer    = buffer;
        _bufferEnd = endBuffer;
    }
}
