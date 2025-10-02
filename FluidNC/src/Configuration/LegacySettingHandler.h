// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "LegacySettingRegistry.h"

namespace Configuration {
    class LegacySettingHandler {
    public:
        inline LegacySettingHandler() { LegacySettingRegistry::registerHandler(this); }

        LegacySettingHandler(const LegacySettingHandler&)            = delete;
        LegacySettingHandler(LegacySettingHandler&&)                 = delete;
        LegacySettingHandler& operator=(const LegacySettingHandler&) = delete;
        LegacySettingHandler& operator=(LegacySettingHandler&&)      = delete;

        virtual uint32_t index()                     = 0;
        virtual void     setValue(const char* value) = 0;

        virtual ~LegacySettingHandler() {
            // Remove from factory? We shouldn't remove handlers...
        }
    };
}
