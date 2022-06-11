#pragma once

#include <cstdint>

typedef uint32_t MotorMask;  // Bits indexed by motor_num*16 + axis
typedef uint16_t AxisMask;   // Bits indexed by axis number
typedef uint8_t  Percent;    // Integer percent
