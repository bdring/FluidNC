// Copyright (c) 2021 -	Stefan de Bruijn
// Copyright (c) 2023 - Dylan Knutson <dymk@dymk.co>
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Tokenizer.h"

#include "parser_logging.h"

#include <cstdlib>
#include <stdexcept>

namespace Configuration {

    Tokenizer::Tokenizer(std::string_view yaml_string) : _remainder(yaml_string), _linenum(0), _token() {}

    bool Tokenizer::isWhiteSpace(char c) {
        return c == ' ' || c == '\t' || c == '\f' || c == '\r';
    }

    bool Tokenizer::isIdentifierChar(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }

    // Finds where a real comment starts in line, matching standard YAML's own
    // rule: a '#' starts a comment when it is preceded by whitespace, or
    // preceded by nothing at all (the very start of line), and is NOT inside
    // a quoted span ("..."/'...', the same simple, non-escaping delimiter
    // convention parseKey()/parseValue() already use elsewhere). A '#' glued
    // directly to other non-whitespace text (no preceding space) is never a
    // comment, quoted or not -- e.g. a macro's G-code parameter reference
    // like '#100' stays literal automatically when it's written mid-value
    // with no space before it; a leading '#100 ...' would still need
    // quoting, same as any other value that starts with '#'.
    //
    // Scans the WHOLE line (not just what will become the value) so a
    // comment can be recognized before parseKey()/parseValue() have even
    // decided where the key ends and the value begins -- exactly like real
    // YAML, which strips comments as a line-level pass before any
    // structural key/value parsing. Returns npos if the line has no real
    // comment in it at all.
    std::string_view::size_type Tokenizer::findCommentStart(std::string_view line) {
        char quote = '\0';
        for (std::string_view::size_type i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (quote) {
                if (c == quote) {
                    quote = '\0';
                }
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                continue;
            }
            if (c == '#' && (i == 0 || isWhiteSpace(line[i - 1]))) {
                return i;
            }
        }
        return std::string_view::npos;
    }

    void Tokenizer::parseError(const std::string_view description) const {
        set_state(State::ConfigAlarm);

        std::string s("Line ");
        s += std::to_string(_linenum);
        s += ": ";
        s += description;
        throw std::runtime_error(s);
    }

    void Tokenizer::parseKey() {
        // entry: first character is not space
        // The first character in the line is neither # nor whitespace
        // (findCommentStart() has already removed any trailing comment,
        // wherever on the line it started -- see nextLine())

        auto delimiter = _line.front();
        if (delimiter == '"' || delimiter == '\'') {
            // Quoted key -- same simple, non-escaping delimiter convention
            // as a quoted value (see parseValue()): the key is exactly the
            // text between the matching quote characters, letting it
            // contain a character (':', '#', whitespace, ...) that would
            // otherwise be significant. No real FluidNC field name needs
            // this today, but it's supported for the same reason a value
            // can be quoted -- consistency with standard YAML, which
            // allows quoting either side of a mapping entry equally.
            _line.remove_prefix(1);
            auto pos = _line.find_first_of(delimiter);
            if (pos == std::string_view::npos) {
                parseError("Did not find matching delimiter");
            }
            _token._key = _line.substr(0, pos);
            _line.remove_prefix(pos + 1);

            // Remove whitespace between the closing quote and the ':'
            while (!_line.empty() && isWhiteSpace(_line.front())) {
                _line.remove_prefix(1);
            }
            if (_line.empty() || _line.front() != ':') {
                std::string err = "Key \"";
                err += _token._key;
                err += "\" must be followed by ':'";
                parseError(err);
            }
            _line.remove_prefix(1);
            return;
        }

        if (!isIdentifierChar(delimiter)) {
            parseError("Invalid character");
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
            parseError(err);
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

            // Disallow initial tabs
            if (_line.front() == '\t') {
                parseError("Use spaces, not tabs, for indentation");
            }

            // Strip a real comment, wherever on the line it starts (see
            // findCommentStart()) -- this covers the old whole-line-comment
            // case (comment starts at position 0, since indentation is
            // already removed above) as well as a same-line trailing
            // comment after a key and/or value, matching standard YAML.
            // Doing this here, before parseKey()/parseValue() ever run,
            // means neither of them needs any comment-awareness of their
            // own -- by the time they see _line, any real comment is
            // already gone, exactly like real YAML strips comments before
            // its own structural key/value parsing begins.
            auto commentStart = findCommentStart(_line);
            if (commentStart != std::string_view::npos) {
                _line.remove_suffix(_line.size() - commentStart);
                while (!_line.empty() && isWhiteSpace(_line.back())) {
                    _line.remove_suffix(1);
                }
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

        // findCommentStart() (see nextLine()) has already removed any real
        // trailing comment from _line, wherever it started -- neither
        // branch below needs any comment-awareness of its own.
        auto delimiter = _line.front();
        if (delimiter == '"' || delimiter == '\'') {
            // Value is quoted
            _line.remove_prefix(1);
            auto pos = _line.find_first_of(delimiter);
            if (pos == std::string_view::npos) {
                parseError("Did not find matching delimiter");
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

    // cppcheck-suppress unusedFunction
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
