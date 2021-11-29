// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Tokenizer.h"

#include "ParseException.h"

#include <cstdlib>

namespace Configuration {

    void Tokenizer::skipToEol() {
        while (!IsEndLine()) {
            Inc();
        }
    }

    Tokenizer::Tokenizer(const char* start, const char* end) : current_(start), end_(end), start_(start), token_() {
        // If start is a yaml document start ('---' [newline]), skip that first.
        if (EqualsCaseInsensitive("---")) {
            for (int i = 0; i < 3; ++i) {
                Inc();
            }
            skipToEol();
            start_ = current_;
        }
    }

    void Tokenizer::ParseError(const char* description) const { throw ParseException(start_, current_, description); }

    void Tokenizer::Tokenize() {
        // Release a held token
        if (token_.state == TokenState::Held) {
            token_.state = TokenState::Matching;
#ifdef DEBUG_VERBOSE_YAML_TOKENIZER
            log_debug("Releasing " << key().str());
#endif
            return;
        }

        // Otherwise find the next token
        token_.state = TokenState::Matching;
        // We parse 1 line at a time. Each time we get here, we can assume that the cursor
        // is at the start of the line.

    parseAgain:
        int indent = 0;

        while (!Eof() && IsSpace()) {
            Inc();
            ++indent;
        }
        token_.indent_ = indent;

        if (Eof()) {
            token_.state     = TokenState::Eof;
            token_.indent_   = -1;
            token_.keyStart_ = token_.keyEnd_ = current_;
            return;
        }
        switch (Current()) {
            case '\t':
                ParseError("Tabs are not allowed. Use spaces for indentation.");
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
                    ParseError("Expected identifier.");
                }

                token_.keyStart_ = current_;
                Inc();
                while (!Eof() && IsIdentifierChar()) {
                    Inc();
                }
                token_.keyEnd_ = current_;

                // Skip whitespaces:
                while (IsWhiteSpace()) {
                    Inc();
                }

                if (Current() != ':') {
                    ParseError("Keys must be followed by ':'");
                }
                Inc();

                // Skip whitespaces after the colon:
                while (IsWhiteSpace()) {
                    Inc();
                }

                // token_.indent_ = indent;
                if (IsEndLine()) {
#ifdef DEBUG_VERBOSE_YAML_TOKENIZER
                    log_debug("Section " << StringRange(token_.keyStart_, token_.keyEnd_).str());
#endif
                    // A key with nothing else is not necessarily a section - it could
                    // be an item whose value is the empty string
                    token_.sValueStart_ = current_;
                    token_.sValueEnd_   = current_;
                    Inc();
                } else {
                    if (Current() == '"' || Current() == '\'') {
                        auto delimiter = Current();

                        Inc();
                        token_.sValueStart_ = current_;
                        while (!Eof() && Current() != delimiter && !IsEndLine()) {
                            Inc();
                        }
                        token_.sValueEnd_ = current_;
                        if (Current() != delimiter) {
                            ParseError("Did not find matching delimiter");
                        }
                        Inc();
#ifdef DEBUG_VERBOSE_YAML_TOKENIZER
                        log_debug("StringQ " << StringRange(token_.keyStart_, token_.keyEnd_).str() << " "
                                             << StringRange(token_.sValueStart_, token_.sValueEnd_).str());
#endif
                    } else {
                        token_.sValueStart_ = current_;
                        while (!IsEndLine()) {
                            Inc();
                        }
                        token_.sValueEnd_ = current_;
                        if (token_.sValueEnd_ != token_.sValueStart_ && token_.sValueEnd_[-1] == '\r') {
                            --token_.sValueEnd_;
                        }
#ifdef DEBUG_VERBOSE_YAML_TOKENIZER
                        log_debug("String " << StringRange(token_.keyStart_, token_.keyEnd_).str() << " "
                                            << StringRange(token_.sValueStart_, token_.sValueEnd_).str());
#endif
                    }
                    skipToEol();
                }
                break;
        }  // switch
    }
}
