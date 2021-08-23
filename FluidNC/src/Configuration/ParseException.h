// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

namespace Configuration {
    class ParseException {
        int         line_;
        int         column_;
        const char* description_;
        const char* current_;

    public:
        ParseException()                      = default;
        ParseException(const ParseException&) = default;

        ParseException(const char* start, const char* current, const char* description) : description_(description), current_(current) {
            line_   = 1;
            column_ = 1;
            while (start != current) {
                if (*start == '\n') {
                    ++line_;
                    column_ = 1;
                }
                ++column_;
                ++start;
            }
        }

        inline int         LineNumber() const { return line_; }
        inline int         ColumnNumber() const { return column_; }
        inline const char* Near() const { return current_; }
        inline const char* What() const { return description_; }
    };
}
