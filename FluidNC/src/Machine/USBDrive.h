// Copyright (c) 2026
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"

namespace Machine {
    class USBDrive : public Configuration::Configurable {
    public:
        bool _enabled = true;
        bool config_ok = false;

        USBDrive() = default;
        ~USBDrive() = default;

        void init();
        void afterParse() override;
        void group(Configuration::HandlerBase& handler) override;
    };
}
