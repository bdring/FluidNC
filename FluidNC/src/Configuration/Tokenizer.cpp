// Copyright (c) 2021 -	Stefan de Bruijn
// Copyright (c) 2023 - Dylan Knutson <dymk@dymk.co>
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Tokenizer.h"

#include "ParseException.h"
#include "parser_logging.h"

#include <cstdlib>

namespace Configuration {

    Tokenizer::Tokenizer(std::string_view yaml_string) : _remainder(yaml_string), _linenum(0), _token() {}

    bool Tokenizer::isWhiteSpace(char c) { return c == ' ' || c == '\t' || c == '\f' || c == '\r'; }

    bool Tokenizer::isIdentifierChar(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }

    void Tokenizer::ParseError(const char* description) const { throw ParseException(_linenum, description); }

    void Tokenizer::parseKey() {
        // entry: first character is not space
        // The first character in the line is neither # nor whitespace
        if (!isIdentifierChar(_line.front())) {
            ParseError("Invalid character");
        }
        auto pos    = _line.find_first_of(':');
        _token._key = _line.substr(0, pos);
        while (isWhiteSpace(_token._key.back())) {
            _token._key.remove_suffix(1);
        }
        if (pos == std::string_view::npos) {
            std::string err = "Key ";
            err += _token._key;
            err += " must be followed by ':'";
            ParseError(err.c_str());
        }
        _line.remove_prefix(pos + 1);
    }

    // Sets _line to the next non-empty, non-comment line.
    // Removes leading spaces setting _token._indent to their number
    // Returns false at end of file
    bool Tokenizer::nextLine() {
        do {
            _linenum++;

            // End of input
            if (_remainder.empty()) {
                _line = _remainder;
                return false;
            }

            // Get next line.  The final line need not have a newline
            auto pos = _remainder.find_first_of('\n');
            if (pos == std::string_view::npos) {
                _line = _remainder;
                _remainder.remove_prefix(_remainder.size());
            } else {
                _line = _remainder.substr(0, pos);
                _remainder.remove_prefix(pos + 1);
            }
            if (_line.empty()) {
                continue;
            }

            // Remove carriage return if present
            if (_line.back() == '\r') {
                _line.remove_suffix(1);
            }
            if (_line.empty()) {
                continue;
            }

            // Remove indentation and record the level
            _token._indent = _line.find_first_not_of(' ');
            if (_token._indent == std::string_view::npos) {
                // Line containing only spaces
                _line.remove_prefix(_line.size());
                continue;
            }
            _line.remove_prefix(_token._indent);

            // Disallow inital tabs
            if (_line.front() == '\t') {
                ParseError("Use spaces, not tabs, for indentation");
            }

            // Discard comment lines
            if (_line.front() == '#') {  // Comment till end of line
                _line.remove_prefix(_line.size());
            }
        } while (_line.empty());

        return true;
    }

    void Tokenizer::parseValue() {
        // Remove initial whitespace
        while (!_line.empty() && isWhiteSpace(_line.front())) {
            _line.remove_prefix(1);
        }

        // Lines with no value are sections
        if (_line.empty()) {
            log_parser_verbose("Section " << _token._key);
            // A key with nothing else is not necessarily a section - it could
            // be an item whose value is the empty string
            _token._value = {};
            return;
        }

        auto delimiter = _line.front();
        if (delimiter == '"' || delimiter == '\'') {
            // Value is quoted
            _line.remove_prefix(1);
            auto pos = _line.find_first_of(delimiter);
            if (pos == std::string_view::npos) {
                ParseError("Did not find matching delimiter");
            }
            _token._value = _line.substr(0, pos);
            _line.remove_prefix(pos + 1);
            log_parser_verbose("StringQ " << _token._key << " " << _token._value);
        } else {
            // Value is not quoted
            _token._value = _line;
            log_parser_verbose("String " << _token._key << " " << _token._value);
        }
    }

    void Tokenizer::Tokenize() {
        // Release a held token
        if (_token._state == TokenState::Held) {
            _token._state = TokenState::Matching;
            log_parser_verbose("Releasing " << key());
            return;
        }

        // Otherwise find the next token
        _token._state = TokenState::Matching;

        // We parse 1 line at a time. Each time we get here, we can assume that the cursor
        // is at the start of the line.

        if (nextLine()) {
            parseKey();
            parseValue();
            return;
        }

        // End of file
        _token._state  = TokenState::Eof;
        _token._indent = -1;
        _token._key    = {};
    }
}
