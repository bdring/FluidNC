// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

namespace Configuration {
    class ParseException {
        uint32_t          _linenum = 0;
        const std::string _description;

    public:
        ParseException()                      = default;
        ParseException(const ParseException&) = default;

        ParseException(uint32_t linenum, const char* description) : _linenum(linenum), _description(description) {}

        inline uint32_t           LineNumber() const { return _linenum; }
        inline const std::string& What() const { return _description; }
    };
}
