// Copyright (c) 2024 Mitch Bradley All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Module.h"

namespace WebUI {
    class OTA : public Module {
    public:
        OTA(const char* name);

        void init() override;
        void deinit() override;
        void poll() override;

        ~OTA();
    };
}
