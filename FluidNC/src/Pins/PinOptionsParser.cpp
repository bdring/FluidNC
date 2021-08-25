// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "PinOptionsParser.h"

#include <cstring>
#include <cctype>
#include <cstdlib>

namespace Pins {
    PinOption::PinOption(char* start, const char* end) : _start(start), _end(end), _key(start), _value(start) { tokenize(); }

    void PinOption::tokenize() {
        if (_start != _end) {
            _key = _start;

            auto i = _start;
            for (; i != _end && (*i) != ':' && (*i) != ';' && (*i) != '='; ++i) {
                *i = ::tolower(*i);
            }

            if (i == _end) {
                // [start, end> is a key; value is nul
                _value = _end;
                _start = i;
            } else if (*i == '=') {
                // Parsing a key-value pair.
                //
                // Mark end of the key, which is now in [start, end>
                *i = '\0';
                ++i;

                _value = i;

                // Parse the value:
                for (; i != _end && (*i) != ':' && (*i) != ';'; ++i) {
                    *i = ::tolower(*i);
                }

                if (i != _end) {
                    *i     = '\0';
                    _start = i + 1;
                } else {
                    _start = i;
                }
            } else {  // must be ':' or ';'
                // [start, i> is a key; value is nul
                _value = i;
                *i     = '\0';
                _start = i + 1;
            }
        } else {
            // Both key and value are nul.
            _key = _value = _end;
        }
    }

    bool PinOption::is(const char* option) const { return !::strcmp(option, _key); }

    int PinOption::iValue() const {
        // Parse to integer
        return ::atoi(_value);
    }

    double PinOption::dValue() const {
        // Parse to integer
        return ::atof(_value);
    }

    PinOption& PinOption ::operator++() {
        tokenize();
        return *this;
    }

    PinOptionsParser::PinOptionsParser(char* buffer, char* endBuffer) : _buffer(buffer), _bufferEnd(endBuffer) {
        // trim whitespaces:
        while (buffer != endBuffer && ::isspace(*buffer)) {
            ++buffer;
        }
        if (buffer != endBuffer) {
            while (buffer - 1 != endBuffer && ::isspace(endBuffer[-1])) {
                --endBuffer;
            }
            *endBuffer = '\0';
        }
        _buffer    = buffer;
        _bufferEnd = endBuffer;
    }
}
