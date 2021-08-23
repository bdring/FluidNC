#pragma once

#include <cstdint>

typedef uint32_t SpindleSpeed;

// Modal Group M7: Spindle control
enum class SpindleState : uint8_t {
    Disable = 0,  // M5 (Default: Must be zero)
    Cw      = 1,  // M3
    Ccw     = 2,  // M4
    Unknown,      // Used for initialization
};
