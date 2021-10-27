// Copyright (c) 2021 - Patrick Horton
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Pins/PinDetail.h"      // pinnum_t

extern void i2s_in_init(pinnum_t ws, pinnum_t bck, pinnum_t data, int num_chips);
    // Start it up - you MUST have checked for good native gpio pins already.
    // Will call I2SIBus::handleValueChange() upon a changed value.
    // Num_chips is needed to right-shift the 32 bit I2S value so that the
    // pins start at the LSB of the value if there are less than 4 74HC165 chips
    // in the chain (i.e. I2SI.0 is pinA on the 1st chip in the chain)
