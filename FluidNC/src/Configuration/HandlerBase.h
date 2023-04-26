// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "HandlerType.h"
#include "../Pin.h"
#include "../EnumItem.h"
#include "../SpindleDatatypes.h"
#include "../UartTypes.h"

#include <IPAddress.h>
#include <string>

namespace Configuration {
    class Configurable;

    typedef struct {
        SpindleSpeed speed;
        float        percent;
        uint32_t     offset;
        uint32_t     scale;
    } speedEntry;

    template <typename BaseType>
    class GenericFactory;

    class HandlerBase {
    protected:
        virtual void enterSection(const char* name, Configurable* value) = 0;
        virtual bool matchesUninitialized(const char* name)              = 0;

        template <typename BaseType>
        friend class GenericFactory;

    public:
        virtual void item(const char* name, bool& value)                                                            = 0;
        virtual void item(const char* name, int32_t& value, int32_t minValue = 0, int32_t maxValue = INT32_MAX)     = 0;
        virtual void item(const char* name, uint32_t& value, uint32_t minValue = 0, uint32_t maxValue = UINT32_MAX) = 0;

        void item(const char* name, uint8_t& value, uint8_t minValue = 0, uint8_t maxValue = UINT8_MAX) {
            int32_t v = int32_t(value);
            item(name, v, int32_t(minValue), int32_t(maxValue));
            value = uint8_t(v);
        }

        virtual void item(const char* name, float& value, float minValue = -3e38, float maxValue = 3e38)  = 0;
        virtual void item(const char* name, std::vector<speedEntry>& value)                               = 0;
        virtual void item(const char* name, UartData& wordLength, UartParity& parity, UartStop& stopBits) = 0;

        virtual void item(const char* name, Pin& value)       = 0;
        virtual void item(const char* name, IPAddress& value) = 0;

        virtual void item(const char* name, int& value, EnumItem* e) = 0;

        virtual void item(const char* name, std::string& value, int minLength = 0, int maxLength = 255) = 0;

        virtual HandlerType handlerType() = 0;

        template <typename T, typename... U>
        void section(const char* name, T*& value, U... args) {
            if (handlerType() == HandlerType::Parser) {
                // For Parser, matchesUninitialized(name) resolves to _parser.is(name)
                if (matchesUninitialized(name)) {
                    Assert(value == nullptr, "Duplicate section %s", name);
                    if (value == nullptr) {
                        value = new T(args...);
                        enterSection(name, value);
                    }
                }
            } else {
                if (value != nullptr) {
                    enterSection(name, value);
                }
            }
        }

        template <typename T>
        void enterFactory(const char* name, T& value) {
            enterSection(name, &value);
        }
    };
}
