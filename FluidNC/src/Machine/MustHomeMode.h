// Copyright (c) 2026 FluidNC contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstdint>

namespace Machine {
    enum class MustHomeMode : uint32_t {
        Disabled = 0,
        Enabled  = 1,
        Strict   = 2,
    };

    constexpr bool alarmUnlockMayClearHoming(MustHomeMode mode) {
        return mode != MustHomeMode::Strict;
    }
}
