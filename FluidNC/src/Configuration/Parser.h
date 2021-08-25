// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Tokenizer.h"
#include "../Pin.h"
#include "../StringRange.h"
#include "../EnumItem.h"
#include "../UartTypes.h"
#include "HandlerBase.h"

#include <stack>
#include <cstring>
#include <IPAddress.h>

namespace Configuration {
    class Parser : public Tokenizer {
        void parseError(const char* description) const;

    public:
        Parser(const char* start, const char* end);

        bool is(const char* expected);

        StringRange             stringValue() const;
        bool                    boolValue() const;
        int                     intValue() const;
        std::vector<speedEntry> speedEntryValue() const;
        float                   floatValue() const;
        Pin                     pinValue() const;
        int                     enumValue(EnumItem* e) const;
        IPAddress               ipValue() const;
        void                    uartMode(UartData& wordLength, UartParity& parity, UartStop& stopBits) const;
    };
}
