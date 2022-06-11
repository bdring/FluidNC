#pragma once

#include <cstdint>

#include "esp_err.h"

class SystemRestartException {};

#define IRAM_ATTR

// From Arduino.h:

void delay(int ms);

// Get time in microseconds since boot.
int64_t esp_timer_get_time();

// this_thread::yield?
#define NOP()                                                                                                                              \
    do {                                                                                                                                   \
    } while (0);

#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

// ESP...

#include "Esp.h"
