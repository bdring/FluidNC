// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

namespace Configuration {
    class ParseException {
        int               _linenum;
        const std::string _description;

    public:
        ParseException()                      = default;
        ParseException(const ParseException&) = default;

        ParseException(int linenum, const char* description) : _linenum(linenum), _description(description) {}

        inline int                LineNumber() const { return _linenum; }
        inline const std::string& What() const { return _description; }
    };
}
