// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "HandlerBase.h"
#include "Configurable.h"

namespace Configuration {
    class Completer : public Configuration::HandlerBase {
    private:
        String _key;
        int    _reqMatch;
        char*  _matchedStr;
        String _currentPath;

        void addCandidate(String fullName);

    protected:
        void enterSection(const char* name, Configuration::Configurable* value) override;
        bool matchesUninitialized(const char* name) override { return false; }

    public:
        Completer(const char* key, int requestedMatch, char* matchedStr);

        int _numMatches;

        void item(const char* name);
        void item(const char* name, bool& value) override { item(name); }
        void item(const char* name, int32_t& value, int32_t minValue, int32_t maxValue) override { item(name); }
        void item(const char* name, uint32_t& value, uint32_t minValue, uint32_t maxValue) override { item(name); }
        void item(const char* name, float& value, float minValue, float maxValue) override { item(name); }
        void item(const char* name, std::vector<speedEntry>& value) override { item(name); }
        void item(const char* name, UartData& wordLength, UartParity& parity, UartStop& stopBits) override { item(name); }
        void item(const char* name, String& value, int minLength, int maxLength) override { item(name); }
        void item(const char* name, Pin& value) { item(name); }
        void item(const char* name, IPAddress& value) override { item(name); }
        void item(const char* name, int& value, EnumItem* e) override { item(name); }

        HandlerType handlerType() override { return HandlerType::Completer; }

        virtual ~Completer();
    };
}
