// Copyright (c) 2021 -	Stefan de Bruijn
// Copyright (c) 2023 -	Dylan Knutson <dymk@dymk.co>
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TokenState.h"
#include "../Config.h"
#include <string_view>

namespace Configuration {

    class Tokenizer {
        std::string_view current_;

        void skipToEol();

        void Inc() {
            if (current_.size() > 0) {
                current_ = current_.substr(1);
            }
        }
        char Current() const { return Eof() ? '\0' : current_[0]; }

        bool IsAlpha() {
            char c = Current();
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }

        bool IsSpace() { return Current() == ' '; }

        bool IsWhiteSpace() {
            if (Eof()) {
                return false;
            }
            char c = Current();
            return c == ' ' || c == '\t' || c == '\f' || c == '\r';
        }

        bool IsIdentifierChar() { return IsAlpha() || IsDigit() || Current() == '_'; }

        bool IsEndLine() { return Eof() || Current() == '\n'; }

        bool IsDigit() {
            char c = Current();
            return (c >= '0' && c <= '9');
        }

    public:
        int line_;

        // Results:
        struct TokenData {
            // The initial value for indent is -1, so when ParserHandler::enterSection()
            // is called to handle the top level of the YAML config file, tokens at
            // indent 0 will be processed.
            TokenData() : key_({}), value_({}), indent_(-1), state(TokenState::Bof) {}
            std::string_view key_;
            std::string_view value_;
            int              indent_;

            TokenState state = TokenState::Bof;
        } token_;

        void ParseError(const char* description) const;

        inline bool Eof() const { return current_.size() == 0; }

    public:
        Tokenizer(std::string_view yaml_string);
        void                    Tokenize();
        inline std::string_view key() const { return token_.key_; }
    };
}
