// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"
#include "WifiConfig.h"

namespace Machine {
    class WifiAPConfig : public WifiConfig {
    public:
        WifiAPConfig() = default;

        int _channel = 1;

        void validate() override {
            WifiConfig::validate();
            Assert(_channel >= 1 && _channel <= 16, "WIFI channel %d is out of bounds", _channel);  // TODO: I guess?
        }

        void group(Configuration::HandlerBase& handler) override {
            WifiConfig::group(handler);
            handler.item("channel", _channel);
        }

        ~WifiAPConfig() = default;
    };
}
