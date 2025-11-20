// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "PinOptionsParser.h"
#include "string_util.h"

#include <cstring>
#include <cctype>
#include <cstdlib>
#include <charconv>

namespace Pins {
    PinOption::PinOption(const std::string_view options) : _options(options) {
        tokenize();
    }

    void PinOption::tokenize() {
        if (_options.length() == 0) {
            _option = _key = _value = {};
            return;
        }
        auto pos = _options.find_first_of(":;");
        _option  = _options.substr(0, pos);
        if (pos == std::string_view::npos) {
            _option = _options;
            _options.remove_prefix(_options.size());
        } else {
            _option = _options.substr(0, pos);
            _options.remove_prefix(pos + 1);
        }

        pos = _option.find_first_of('=');
        if (pos == std::string_view::npos) {
            _key   = _option;
            _value = {};
        } else {
            _key   = _option.substr(0, pos);
            _value = _option.substr(pos + 1);
        }
    }

    // cppcheck-suppress unusedFunction
    bool PinOption::is(const char* option) const {
        return string_util::equal_ignore_case(_key, option);
    }

    // cppcheck-suppress unusedFunction
    int32_t PinOption::iValue() const {
        // Parse to integer
        int32_t num;
        auto [ptr, ec] = std::from_chars(_value.data(), _value.data() + _value.length(), num);
        return num;
    }

    PinOption& PinOption ::operator++() {
        tokenize();
        return *this;
    }

    PinOptionsParser::PinOptionsParser(std::string_view options) : _options(string_util::trim(options)) {}
}
