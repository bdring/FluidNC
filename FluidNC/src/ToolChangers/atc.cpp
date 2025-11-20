// Copyright (c) 2024 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "atc.h"

#include "Machine/MachineConfig.h"

ATCs::ATC* atc = nullptr;

namespace ATCs {
    void probe_notification() {}

    bool tool_change(tool_t value, bool pre_select) {
        return true;
    }
}
