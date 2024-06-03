#pragma once

#include <cstdint>

typedef uint32_t SpindleSpeed;

// Modal Group M7: Spindle control
enum class SpindleState : uint8_t {
    Disable = 5,  // M5 (Default: Must be zero)
    Cw      = 3,  // M3
    Ccw     = 4,  // M4
    Unknown = 0,  // Used for initialization
};
