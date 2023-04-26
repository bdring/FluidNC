// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"
#include "WifiConfig.h"

namespace Machine {
    class WifiSTAConfig : public WifiConfig {
    public:
        WifiSTAConfig() = default;

        void validate() override { WifiConfig::validate(); }

        void group(Configuration::HandlerBase& handler) override { WifiConfig::group(handler); }

        ~WifiSTAConfig() = default;
    };
}
