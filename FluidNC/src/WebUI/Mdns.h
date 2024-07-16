// Copyright (c) 2024 Mitch Bradley All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "src/Module.h"

namespace WebUI {
    class Mdns : public Module {
    public:
        Mdns();

        static void end();

        void init() override;
        void poll() override;

        ~Mdns();
    };
}
