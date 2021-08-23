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

#include "LegacySettingRegistry.h"

#include "LegacySettingHandler.h"

namespace Configuration {
    bool LegacySettingRegistry::isLegacySetting(const char* str) { return str[0] == '$' && (str[1] >= '0' && str[1] <= '9'); }

    void LegacySettingRegistry::registerHandler(LegacySettingHandler* handler) { instance().handlers_.push_back(handler); }

    bool LegacySettingRegistry::tryHandleLegacy(const char* str) {
        if (isLegacySetting(str)) {
            auto start = str;

            int value = 0;
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

    void LegacySettingRegistry::tryLegacy(int index, const char* value) {
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
