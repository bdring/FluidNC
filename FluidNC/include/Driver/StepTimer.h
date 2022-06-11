#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

void stepTimerInit(uint32_t frequency, bool (*fn)(void));
void stepTimerStop();
void stepTimerSetTicks(uint32_t ticks);
void stepTimerStart();
void stepTimerRestart();

uint32_t stepTimerGetTicks();

#ifdef __cplusplus
}
#endif
