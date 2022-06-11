// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "PinDetail.h"

#include "../Assert.h"
#include <esp_attr.h>  // IRAM_ATTR

namespace Pins {
    void PinDetail::attachInterrupt(void (*callback)(void*), void* arg, int mode) {
        Assert(false, "Interrupts are not supported by pin %d", _index);
    }
    void PinDetail::detachInterrupt() {
        Assert(false, "Interrupts are not supported by pin %d", _index);
        ;
    }
    void IRAM_ATTR PinDetail::synchronousWrite(int high) { write(high); }

}
