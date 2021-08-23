/*
    Part of Grbl_ESP32
    2021 -  Stefan de Bruijn

    Grbl_ESP32 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Grbl_ESP32 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Grbl_ESP32.  If not, see <http://www.gnu.org/licenses/>.
*/

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
