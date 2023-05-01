// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

namespace Configuration {
    class ParseException {
        int         line_;
        std::string description_;

    public:
        ParseException()                      = default;
        ParseException(const ParseException&) = default;

        ParseException(int line, const char* description) : line_(line), description_(description) {}

        inline int                LineNumber() const { return line_; }
        inline const std::string& What() const { return description_; }
    };
}
