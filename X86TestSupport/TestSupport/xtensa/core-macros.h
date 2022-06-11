#pragma once

#include "../esp32-hal-timer.h"

inline int32_t IRAM_ATTR XTHAL_GET_CCOUNT() {
    return int32_t(millis());
}
