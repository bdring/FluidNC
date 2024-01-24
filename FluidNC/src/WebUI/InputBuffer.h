// Copyright (c) 2021 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Channel.h"

namespace WebUI {
    class InputBuffer : public Channel {
    public:
        InputBuffer();

        size_t write(uint8_t c) override { return 0; }
        int    availableforwrite() { return 0; };

        operator bool() const;

        ~InputBuffer();
    };

    extern InputBuffer inputBuffer;
}
