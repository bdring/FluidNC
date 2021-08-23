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
