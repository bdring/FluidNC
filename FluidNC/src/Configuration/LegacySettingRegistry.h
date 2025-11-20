// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <vector>
#include "Config.h"

namespace Configuration {
    class LegacySettingHandler;

    class LegacySettingRegistry {
        static LegacySettingRegistry& instance() {
            static LegacySettingRegistry instance_;
            return instance_;
        }

        LegacySettingRegistry() = default;

        LegacySettingRegistry(const LegacySettingRegistry&)            = delete;
        LegacySettingRegistry& operator=(const LegacySettingRegistry&) = delete;

        std::vector<LegacySettingHandler*> handlers_;

        static bool isLegacySetting(const char* str);
        static void tryLegacy(uint32_t index, const char* value);

    public:
        static void registerHandler(LegacySettingHandler* handler);
        static bool tryHandleLegacy(const char* str);
    };
}
