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

#include "TokenState.h"
#include "../Logging.h"

namespace Configuration {

    class Tokenizer {
        const char* current_;
        const char* end_;

        void skipToEol();

        inline void Inc() {
            if (current_ != end_) {
                ++current_;
            }
        }
        inline char Current() const { return Eof() ? '\0' : (*current_); }

        inline bool IsAlpha() {
            char c = Current();
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }

        inline bool IsSpace() { return Current() == ' '; }

        inline bool IsWhiteSpace() {
            if (Eof()) {
                return false;
            }
            char c = Current();
            return c == ' ' || c == '\t' || c == '\f' || c == '\r';
        }

        inline bool IsEndLine() { return Eof() || Current() == '\n'; }

        inline bool IsDigit() {
            char c = Current();
            return (c >= '0' && c <= '9');
        }

        inline char ToLower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

        inline bool EqualsCaseInsensitive(const char* input) {
            const char* tmp = current_;
            while (ToLower(*input) == ToLower(Current()) && *input != '\0') {
                Inc();
                ++input;
            }

            bool isSame = *input == '\0';  // Everything till the end of the input string is the same
            current_    = tmp;             // Restore situation
            return isSame;
        }

    public:
        const char* start_;

        // Results:
        struct TokenData {
            // The initial value for indent is -1, so when ParserHandler::enterSection()
            // is called to handle the top level of the YAML config file, tokens at
            // indent 0 will be processed.
            TokenData() :
                keyStart_(nullptr), keyEnd_(nullptr), indent_(-1), state(TokenState::Bof), sValueStart_(nullptr), sValueEnd_(nullptr) {}

            const char* keyStart_;
            const char* keyEnd_;
            int         indent_;

            TokenState state = TokenState::Bof;

            const char* sValueStart_;
            const char* sValueEnd_;
        } token_;

        void ParseError(const char* description) const;

        inline bool Eof() const { return current_ == end_; }

    public:
        Tokenizer(const char* start, const char* end);
        void Tokenize();

        inline StringRange key() const { return StringRange(token_.keyStart_, token_.keyEnd_); }
    };
}
