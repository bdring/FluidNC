// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "HandlerBase.h"
#include "Parser.h"
#include "Configurable.h"
#include "../System.h"

#include "../Logging.h"

#include <vector>

// #define DEBUG_VERBOSE_YAML_PARSER
// #define DEBUG_CHATTY_YAML_PARSER
namespace Configuration {
    class ParserHandler : public Configuration::HandlerBase {
    private:
        Configuration::Parser&   _parser;
        std::vector<const char*> _path;

    public:
        void enterSection(const char* name, Configuration::Configurable* section) override {
            _path.push_back(name);  // For error handling

            // On entry, the token is for the section that invoked us.
            // We will handle following nodes with indents greater than entryIndent
            int entryIndent = _parser.token_.indent_;
#ifdef DEBUG_CHATTY_YAML_PARSER
            log_debug("Entered section " << name << " at indent " << entryIndent);
#endif

            // The next token controls what we do next.  If thisIndent is greater
            // than entryIndent, there are some subordinate tokens.
            _parser.Tokenize();
            int thisIndent = _parser.token_.indent_;
#ifdef DEBUG_VERBOSE_YAML_PARSER
            log_debug("thisIndent " << _parser.key().str() << " " << thisIndent);
#endif

            // If thisIndent <= entryIndent, the section is empty - there are
            // no more-deeply-indented subordinate tokens.

            if (thisIndent > entryIndent) {
                // If thisIndent > entryIndent, the new token is the first token within
                // this section so we process tokens at the same level as thisIndent.
                for (; _parser.token_.indent_ >= thisIndent; _parser.Tokenize()) {
#ifdef DEBUG_VERBOSE_YAML_PARSER
                    log_debug(" KEY " << _parser.key().str() << " state " << int(_parser.token_.state) << " indent "
                                      << _parser.token_.indent_);
#endif
                    if (_parser.token_.indent_ > thisIndent) {
                        log_error("Skipping key " << _parser.key().str() << " indent " << _parser.token_.indent_ << " thisIndent "
                                                 << thisIndent);
                    } else {
#ifdef DEBUG_VERBOSE_YAML_PARSER
                        log_debug("Parsing key " << _parser.key().str());
#endif
                        try {
                            section->group(*this);
                        } catch (const AssertionFailed& ex) {
                            // Log something meaningful to the user:
                            log_error("Configuration error at "; for (auto it : _path) { ss << '/' << it; } ss << ": " << ex.msg);

                            // Set the state to config alarm, so users can't run time machine.
                            sys.state = State::ConfigAlarm;
                        }

                        if (_parser.token_.state == TokenState::Matching) {
                            log_warn("Ignored key " << _parser.key().str());
                        }
#ifdef DEBUG_CHATTY_YAML_PARSER
                        if (_parser.token_.state == Configuration::TokenState::Matched) {
                            log_debug("Handled key " << _parser.key().str());
                        }
#endif
                    }
                }
            }

            // At this point we have the next token whose indent we
            // needed in order to decide what to do.  When we return,
            // the caller will call Tokenize() to get a token, so we
            // "hold" the current token so that Tokenize() will
            // release that token instead of parsing ahead.
            // _parser.token_.held = true;

            _parser.token_.state = TokenState::Held;
#ifdef DEBUG_CHATTY_YAML_PARSER
            log_debug("Left section at indent " << entryIndent << " holding " << _parser.key().str());
#endif

            _path.erase(_path.begin() + (_path.size() - 1));
        }

        bool matchesUninitialized(const char* name) override { return _parser.is(name); }

    public:
        ParserHandler(Configuration::Parser& parser) : _parser(parser) {}

        void item(const char* name, int32_t& value, int32_t minValue, int32_t maxValue) override {
            if (_parser.is(name)) {
                value = _parser.intValue();
            }
        }

        void item(const char* name, int& value, EnumItem* e) override {
            if (_parser.is(name)) {
                value = _parser.enumValue(e);
            }
        }

        void item(const char* name, bool& value) override {
            if (_parser.is(name)) {
                value = _parser.boolValue();
            }
        }

        void item(const char* name, float& value, float minValue, float maxValue) override {
            if (_parser.is(name)) {
                value = _parser.floatValue();
            }
        }

        void item(const char* name, std::vector<speedEntry>& value) override {
            if (_parser.is(name)) {
                value = _parser.speedEntryValue();
            }
        }

        void item(const char* name, UartData& wordLength, UartParity& parity, UartStop& stopBits) override {
            if (_parser.is(name)) {
                _parser.uartMode(wordLength, parity, stopBits);
            }
        }

        void item(const char* name, String& value, int minLength, int maxLength) override {
            if (_parser.is(name)) {
                value = _parser.stringValue().str();
            }
        }

        void item(const char* name, Pin& value) override {
            if (_parser.is(name)) {
                auto parsed = _parser.pinValue();
                value.swap(parsed);
            }
        }

        void item(const char* name, IPAddress& value) override {
            if (_parser.is(name)) {
                value = _parser.ipValue();
            }
        }

        HandlerType handlerType() override { return HandlerType::Parser; }
    };
}
