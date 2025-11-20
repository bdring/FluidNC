// Copyright (c) 2021 -	Stefan de Bruijn
// Copyright (c) 2023 -	Dylan Knutson <dymk@dymk.co>
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TokenState.h"
#include "Config.h"
#include <string_view>

namespace Configuration {

    class Tokenizer {
        std::string_view _remainder;

        bool isWhiteSpace(char c);
        bool isIdentifierChar(char c);
        bool nextLine();
        void parseKey();
        void parseValue();

    public:
        uint32_t         _linenum;
        std::string_view _line;

        // Results:
        struct TokenData {
            // The initial value for indent is -1, so when ParserHandler::enterSection()
            // is called to handle the top level of the YAML config file, tokens at
            // indent 0 will be processed.
            TokenData() : _key(), _value(), _indent(-1), _state(TokenState::Bof) {}
            std::string_view _key;
            std::string_view _value;
            int_fast8_t      _indent;

            TokenState _state = TokenState::Bof;
        } _token;

        void parseError(const std::string_view description) const;

    public:
        explicit Tokenizer(std::string_view yaml_string);
        void                    Tokenize();
        inline std::string_view key() const { return _token._key; }
    };
}
