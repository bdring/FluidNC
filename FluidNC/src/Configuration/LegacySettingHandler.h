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

#include "LegacySettingRegistry.h"

namespace Configuration {
    class LegacySettingHandler {
    public:
        inline LegacySettingHandler() { LegacySettingRegistry::registerHandler(this); }

        LegacySettingHandler(const LegacySettingHandler&) = delete;
        LegacySettingHandler(LegacySettingHandler&&)      = delete;
        LegacySettingHandler& operator=(const LegacySettingHandler&) = delete;
        LegacySettingHandler& operator=(LegacySettingHandler&&) = delete;

        virtual int  index()                     = 0;
        virtual void setValue(const char* value) = 0;

        virtual ~LegacySettingHandler() {
            // Remove from factory? We shouldn't remove handlers...
        }
    };
}
