#include "Driver/random.h"

#include <esp_random.h>

uint32_t random_u32() {
    return esp_random();
}
