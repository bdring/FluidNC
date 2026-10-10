// Copyright (c) 2024 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstdint>
#include <string_view>

namespace Spindles {
    namespace VFD {
        // Applies a ModbusVFD scale suffix like "%*100", "*60/100" or "/60" to n.
        // '%' converts n to a percentage of maxRPM before the multiply/divide.
        // Intermediate math is 64-bit so e.g. 24000 with "%*16384/100" does not overflow.
        // Returns false, leaving n unchanged, if a number is malformed or a divisor is 0.
        bool modbus_scale(uint32_t& n, std::string_view scale_str, uint32_t maxRPM);
    }
}
