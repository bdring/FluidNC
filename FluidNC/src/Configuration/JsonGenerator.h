// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <vector>

#include "../Pin.h"
#include "HandlerBase.h"

#include "../WebUI/JSONEncoder.h"

namespace Configuration {
    class Configurable;

    class JsonGenerator : public HandlerBase {
        JsonGenerator(const JsonGenerator&) = delete;
        JsonGenerator& operator=(const JsonGenerator&) = delete;

        char                _currentPath[256];  // 256 = max size of configuration string.
        char*               _paths[16];         // 16 = max depth. Points to the _end_ of a string in currentPath
        int                 _depth;             // current depth. Used along with _paths
        WebUI::JSONencoder& _encoder;

        void enter(const char* name);
        void add(Configuration::Configurable* configurable);
        void leave();

    protected:
        void        enterSection(const char* name, Configurable* value) override;
        bool        matchesUninitialized(const char* name) override { return false; }
        HandlerType handlerType() override { return HandlerType::Generator; }

    public:
        JsonGenerator(WebUI::JSONencoder& encoder);

        void item(const char* name, bool& value) override;
        void item(const char* name, int& value, const int32_t minValue, const int32_t maxValue) override;
        void item(const char* name, uint32_t& value, const uint32_t minValue, const uint32_t maxValue) override;
        void item(const char* name, float& value, const float minValue, const float maxValue) override;
        void item(const char* name, std::vector<speedEntry>& value) override;
        void item(const char* name, UartData& wordLength, UartParity& parity, UartStop& stopBits) override;
        void item(const char* name, std::string& value, const int minLength, const int maxLength) override;
        void item(const char* name, Pin& value) override;
        void item(const char* name, IPAddress& value) override;
        void item(const char* name, int& value, const EnumItem* e) override;
    };
}
