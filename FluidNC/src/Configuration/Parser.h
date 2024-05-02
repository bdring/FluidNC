// Copyright (c) 2021 -	Stefan de Bruijn
// Copyright (c) 2023 -	Dylan Knutson <dymk@dymk.co>
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Tokenizer.h"
#include "../Pin.h"
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
        Parser(std::string_view yaml_string);

        bool is(const char* expected);

        std::string_view        stringValue() const;
        bool                    boolValue() const;
        int                     intValue() const;
        uint32_t                uintValue() const;
        std::vector<speedEntry> speedEntryValue() const;
        float                   floatValue() const;
        Pin                     pinValue() const;
        int                     enumValue(const EnumItem* e) const;
        IPAddress               ipValue() const;
        void                    uartMode(UartData& wordLength, UartParity& parity, UartStop& stopBits) const;
    };
}
