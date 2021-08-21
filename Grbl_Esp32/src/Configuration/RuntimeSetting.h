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

#include "HandlerBase.h"
#include "Configurable.h"
#include "../WebUI/ESPResponse.h"

namespace Configuration {
    class RuntimeSetting : public Configuration::HandlerBase {
    private:
        const char* setting_;  // foo/bar
        const char* start_;

        const char* newValue_;  // null (read) or 123 (value)

        WebUI::ESPResponseStream* out_;

        bool is(const char* name) const {
            if (start_ != nullptr) {
                auto len    = strlen(name);
                auto result = !strncasecmp(name, start_, len) && (start_[len] == '\0' || start_[len] == '/');
                return result;
            } else {
                return false;
            }
        }

    protected:
        void enterSection(const char* name, Configuration::Configurable* value) override;
        bool matchesUninitialized(const char* name) override { return false; }

    public:
        RuntimeSetting(const char* key, const char* value, WebUI::ESPResponseStream* out);

        void item(const char* name, bool& value) override;
        void item(const char* name, int32_t& value, int32_t minValue, int32_t maxValue) override;
        void item(const char* name, float& value, float minValue, float maxValue) override;
        void item(const char* name, std::vector<speedEntry>& value) override {}
        void item(const char* name, UartData& wordLength, UartParity& parity, UartStop& stopBits) override {}
        void item(const char* name, String& value, int minLength, int maxLength) override;
        void item(const char* name, Pin& value) override;
        void item(const char* name, IPAddress& value) override;
        void item(const char* name, int& value, EnumItem* e) override;

        HandlerType handlerType() override { return HandlerType::Runtime; }

        bool isHandled_ = false;

        virtual ~RuntimeSetting();
    };
}
