// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "PinDetail.h"

namespace Pins {
    // cppcheck-suppress unusedFunction
    void PinDetail::registerEvent(InputPin* obj) {
        Assert(false, "registerEvent is not supported by pin %d", _index);
    }

    // cppcheck-suppress unusedFunction
    void IRAM_ATTR PinDetail::synchronousWrite(bool high) {
        write(high);
    }
}
