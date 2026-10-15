#include "Driver/random.h"

#include <Arduino.h>  // rp2040

uint32_t random_u32() {
    return rp2040.hwrand32();
}
