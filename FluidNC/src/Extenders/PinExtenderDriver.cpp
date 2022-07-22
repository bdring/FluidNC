// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "PinExtenderDriver.h"

namespace Extenders {

    void PinExtenderDriver::attachInterrupt(pinnum_t index, void (*callback)(void*), void* arg, int mode) {
        Assert(false, "Interrupts are not supported by pin extender for pin %d", index);
    }
    void PinExtenderDriver::detachInterrupt(pinnum_t index) { Assert(false, "Interrupts are not supported by pin extender"); }
}
