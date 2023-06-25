// Copyright (c) 2021 -	Stefan de Bruijn
// Copyright (c) 2023 - Dylan Knutson <dymk@dymk.co>
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Tokenizer.h"

#include "ParseException.h"
#include "parser_logging.h"

#include <cstdlib>

namespace Configuration {

    void Tokenizer::skipToEol() {
        while (!IsEndLine()) {
            Inc();
        }
        Inc();
    }

    Tokenizer::Tokenizer(std::string_view yaml_string) : current_(yaml_string), line_(0), token_() {}

    void Tokenizer::ParseError(const char* description) const { throw ParseException(line_, description); }

    void Tokenizer::Tokenize() {
        // Release a held token
        if (token_.state == TokenState::Held) {
            token_.state = TokenState::Matching;
            log_parser_verbose("Releasing " << key());
            return;
        }

        // Otherwise find the next token
        token_.state = TokenState::Matching;
        // We parse 1 line at a time. Each time we get here, we can assume that the cursor
        // is at the start of the line.

    parseAgain:
        ++line_;

        int indent = 0;

        while (!Eof() && IsSpace()) {
            Inc();
            ++indent;
        }
        token_.indent_ = indent;

        if (Eof()) {
            token_.state   = TokenState::Eof;
            token_.indent_ = -1;
            token_.key_    = {};
            return;
        }
        switch (Current()) {
            case '\t':
                ParseError("Use spaces, not tabs, for indentation");
                break;

            case '#':  // Comment till end of line
                Inc();
                while (!Eof() && !IsEndLine()) {
                    Inc();
                }
                goto parseAgain;

            case '\r':
                Inc();
                if (!Eof() && Current() == '\n') {
                    Inc();
                }  // \r\n
                goto parseAgain;
            case '\n':
                // \n without a preceding \r
                Inc();
                goto parseAgain;

            default:
                if (!IsIdentifierChar()) {
                    ParseError("Invalid character");
                }

                const auto tok_start = current_.cbegin();
                Inc();
                while (!Eof() && IsIdentifierChar()) {
                    Inc();
                }
                token_.key_ = { tok_start, static_cast<size_t>(current_.cbegin() - tok_start) };

                // Skip whitespaces:
                while (IsWhiteSpace()) {
                    Inc();
                }

                if (Current() != ':') {
                    std::string err = "Key ";
                    err += token_.key_;
                    err += " must be followed by ':'";
                    ParseError(err.c_str());
                }
                Inc();

                // Skip whitespaces after the colon:
                while (IsWhiteSpace()) {
                    Inc();
                }

                // token_.indent_ = indent;
                if (IsEndLine()) {
                    log_parser_verbose("Section " << token_.key_);
                    // A key with nothing else is not necessarily a section - it could
                    // be an item whose value is the empty string
                    token_.value_ = {};
                    Inc();
                } else {
                    if (Current() == '"' || Current() == '\'') {
                        auto delimiter = Current();

                        Inc();
                        const auto tok_start = current_.cbegin();
                        while (!Eof() && Current() != delimiter && !IsEndLine()) {
                            Inc();
                        }
                        token_.value_ = { tok_start, static_cast<size_t>(current_.cbegin() - tok_start) };
                        if (Current() != delimiter) {
                            ParseError("Did not find matching delimiter");
                        }
                        Inc();
                        log_parser_verbose("StringQ " << token_.key_ << " " << token_.value_);
                    } else {
                        const auto tok_start = current_.cbegin();
                        while (!IsEndLine()) {
                            Inc();
                        }
                        auto& value_tok = token_.value_;
                        value_tok       = { tok_start, static_cast<size_t>(current_.cbegin() - tok_start) };
                        if (!value_tok.empty() && value_tok[value_tok.size() - 1] == '\r') {
                            value_tok.remove_suffix(1);
                        }
                        log_parser_verbose("String " << token_.key_ << " " << token_.value_);
                    }
                    skipToEol();
                }
                break;
        }  // switch
    }
}
