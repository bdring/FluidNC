#include "Driver/random.h"

#include <random>

uint32_t random_u32() {
    static std::random_device rd;
    return rd();
}
