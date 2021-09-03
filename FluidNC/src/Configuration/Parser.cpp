// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Parser.h"

#include "ParseException.h"
#include "../EnumItem.h"

#include "../Logging.h"

#include <climits>
#include <math.h>  // round

namespace Configuration {
    Parser::Parser(const char* start, const char* end) : Tokenizer(start, end) {}

    void Parser::parseError(const char* description) const {
        // Attempt to use the correct position in the parser:
        if (token_.keyEnd_) {
            throw ParseException(token_.keyStart_, token_.keyEnd_, description);
        } else {
            Tokenizer::ParseError(description);
        }
    }

    bool Parser::is(const char* expected) {
        if (token_.state != TokenState::Matching || token_.keyStart_ == nullptr) {
            return false;
        }
        auto len = strlen(expected);
        if (len != (token_.keyEnd_ - token_.keyStart_)) {
            return false;
        }
        bool result = !strncasecmp(expected, token_.keyStart_, len);
        if (result) {
            token_.state = TokenState::Matched;
        }
        return result;
    }

    StringRange Parser::stringValue() const { return StringRange(token_.sValueStart_, token_.sValueEnd_); }

    bool Parser::boolValue() const {
        auto str = StringRange(token_.sValueStart_, token_.sValueEnd_);
        return str.equals("true");
    }

    int Parser::intValue() const {
        auto    str = StringRange(token_.sValueStart_, token_.sValueEnd_);
        int32_t value;
        if (str.isInteger(value)) {
            return value;
        }
        float fvalue;
        if (str.isFloat(fvalue)) {
            return int(round(fvalue));
        }
        parseError("Expected an integer value");
        return 0;
    }

    float Parser::floatValue() const {
        auto  str = StringRange(token_.sValueStart_, token_.sValueEnd_);
        float value;
        if (!str.isFloat(value)) {
            parseError("Expected a float value like 123.456");
        }
        return value;
    }

    std::vector<speedEntry> Parser::speedEntryValue() const {
        auto str = StringRange(token_.sValueStart_, token_.sValueEnd_);

        std::vector<speedEntry> value;
        StringRange             entryStr;
        for (entryStr = str.nextWord(); entryStr.length(); entryStr = str.nextWord()) {
            speedEntry  entry;
            StringRange speed = entryStr.nextWord('=');
            if (!speed.length() || !speed.isUInteger(entry.speed)) {
                log_error("Bad speed number " << speed.str());
                break;
            }
            StringRange percent = entryStr.nextWord('%');
            if (!percent.length() || !percent.isFloat(entry.percent)) {
                log_error("Bad speed percent " << percent.str());
                break;
            }
            value.push_back(entry);
        }
        return value;
    }

    Pin Parser::pinValue() const {
        auto str = StringRange(token_.sValueStart_, token_.sValueEnd_);
        return Pin::create(str);
    }

    IPAddress Parser::ipValue() const {
        IPAddress ip;
        auto      str = StringRange(token_.sValueStart_, token_.sValueEnd_);
        if (!ip.fromString(str.str())) {
            parseError("Expected an IP address like 192.168.0.100");
        }
        return ip;
    }

    int Parser::enumValue(EnumItem* e) const {
        auto str = StringRange(token_.sValueStart_, token_.sValueEnd_);
        for (; e->name; ++e) {
            if (str.equals(e->name)) {
                break;
            }
        }
        return e->value;  // Terminal value is default.
    }

    void Parser::uartMode(UartData& wordLength, UartParity& parity, UartStop& stopBits) const {
        auto str = StringRange(token_.sValueStart_, token_.sValueEnd_);
        if (str.length() == 5 || str.length() == 3) {
            int32_t wordLenInt;
            if (!str.subString(0, 1).isInteger(wordLenInt)) {
                parseError("Uart mode should be specified as [Bits Parity Stopbits] like [8N1]");
            } else if (wordLenInt < 5 || wordLenInt > 8) {
                parseError("Number of data bits for uart is out of range. Expected format like [8N1].");
            }
            wordLength = UartData(int(UartData::Bits5) + (wordLenInt - 5));

            switch (str.begin()[1]) {
                case 'N':
                    parity = UartParity::None;
                    break;
                case 'O':
                    parity = UartParity::Odd;
                    break;
                case 'E':
                    parity = UartParity::Even;
                    break;
                default:
                    parseError("Uart mode should be specified as [Bits Parity Stopbits] like [8N1]");
                    break;  // Omits compiler warning. Never hit.
            }

            auto stop = str.subString(2, str.length() - 2);
            if (stop.equals("1")) {
                stopBits = UartStop::Bits1;
            } else if (stop.equals("1.5")) {
                stopBits = UartStop::Bits1_5;
            } else if (stop.equals("2")) {
                stopBits = UartStop::Bits2;
            } else {
                parseError("Uart stopbits can only be 1, 1.5 or 2. Syntax is [8N1]");
            }

        } else {
            parseError("Uart mode should be specified as [Bits Parity Stopbits] like [8N1]");
        }
    }
}
