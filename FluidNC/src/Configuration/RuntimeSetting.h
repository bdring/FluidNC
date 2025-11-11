// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "HandlerBase.h"
#include "Configurable.h"

namespace Configuration {
    class RuntimeSetting : public Configuration::HandlerBase {
    private:
        std::string_view setting_;  // foo/bar
        std::string_view start_;

        std::string_view newValue_;  // null (read) or 123 (value)

        Channel& out_;

        bool is(std::string_view name) const {
            if (start_.empty()) {
                return false;
            }
            return string_util::starts_with_ignore_case(start_, name) && (start_.length() == name.length() || start_[name.length()] == '/');
        }

    protected:
        void enterSection(const char* name, Configuration::Configurable* value) override;
        bool matchesUninitialized(const char* name) override { return false; }

    public:
        RuntimeSetting(std::string_view key, std::string_view value, Channel& out);

        void item(const char* name, bool& value) override;
        void item(const char* name, int32_t& value, const int32_t minValue, const int32_t maxValue) override;
        void item(const char* name, uint32_t& value, const uint32_t minValue, const uint32_t maxValue) override;
        void item(const char* name, float& value, const float minValue, const float maxValue) override;
        void item(const char* name, std::vector<speedEntry>& value) override;
        void item(const char* name, std::vector<float>& value) override;
        void item(const char* name, UartData& wordLength, UartParity& parity, UartStop& stopBits) override;
        void item(const char* name, std::string& value, const int minLength, const int maxLength) override;
        void item(const char* name, EventPin& value) override;
        void item(const char* name, InputPin& value) override;
        void item(const char* name, Pin& value) override;
        void item(const char* name, Macro& value) override;
        void item(const char* name, IPAddress& value) override;
        void item(const char* name, uint32_t& value, const EnumItem* e) override;
        void item(const char* name, axis_t& value) override;

        std::string setting_prefix();

        HandlerType handlerType() override { return HandlerType::Runtime; }

        bool isHandled_ = false;

        virtual ~RuntimeSetting();
    };
}
