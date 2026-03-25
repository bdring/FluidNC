#pragma once

#include <stdint.h>
#include <stdbool.h>

uint32_t stepTimerInit(bool (*fn)(void));

void stepTimerStop();
void stepTimerSetTicks(uint32_t ticks);
void stepTimerStart();
