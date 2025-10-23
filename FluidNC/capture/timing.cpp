#include "Driver/delay_usecs.h"

static int counter = 0;
uint32_t   ticks_per_us;

void timing_init() {
    ticks_per_us = 1;
}

void delay_us(int32_t us) {
    spinUntil(usToEndTicks(us));
}

int32_t usToCpuTicks(int32_t us) {
    return us * ticks_per_us;
}

int32_t usToEndTicks(int32_t us) {
    return getCpuTicks() + usToCpuTicks(us);
}

void spinUntil(int32_t endTicks) {
    while ((getCpuTicks() - endTicks) < 0) {}
}

int32_t getCpuTicks() {
    return ++counter;
}
