// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "LegacySettingRegistry.h"

#include "LegacySettingHandler.h"

namespace Configuration {
    bool LegacySettingRegistry::isLegacySetting(const char* str) {
        return str[0] == '$' && (str[1] >= '0' && str[1] <= '9');
    }

    void LegacySettingRegistry::registerHandler(LegacySettingHandler* handler) {
        instance().handlers_.push_back(handler);
    }

    // cppcheck-suppress unusedFunction
    bool LegacySettingRegistry::tryHandleLegacy(const char* str) {
        if (isLegacySetting(str)) {
            auto start = str;

            uint32_t value = 0;
            ++str;

            while (*str && *str >= '0' && *str <= '9') {
                value = value * 10 + (*str - '0');
                ++str;
            }

            if (*str == '=') {
                ++str;

                tryLegacy(value, str);
            } else {
                log_warn("Incorrect setting '" << start << "': cannot find '='.");
            }
            return true;
        } else {
            return false;
        }
    }

    void LegacySettingRegistry::tryLegacy(uint32_t index, const char* value) {
        bool handled = false;
        for (auto it : instance().handlers_) {
            if (it->index() == index) {
                handled = true;
                it->setValue(value);
                // ??? Show we break here, or are index duplications allowed?
            }
        }

        if (!handled) {
            log_warn("Cannot find handler for $" << index << ". Setting was ignored.");
        }
    }
}
