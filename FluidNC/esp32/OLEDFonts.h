#pragma once

#include <cstdint>

// Fonts for the OLED display, ASCII (32..126) only.  OLED.cpp maps every other
// character to '?' so nothing indexes past these tables.
extern const uint8_t FluidNC_Arial_16[];
