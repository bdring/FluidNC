#pragma once

#include <cstdint>
#include "esp_err.h"

extern uint32_t g_ticks_per_us_pro;  // For CPU 0 - typically 240 MHz
extern uint32_t g_ticks_per_us_app;  // For CPU 1 - typically 240 MHz

// Timer:

struct hw_timer_s;
typedef struct hw_timer_s hw_timer_t;

void timerAlarmEnable(hw_timer_t* timer);
void timerAlarmDisable(hw_timer_t* timer);
void timerWrite(hw_timer_t* timer, uint64_t val);
void timerAlarmWrite(hw_timer_t* timer, uint64_t interruptAt, bool autoreload);

hw_timer_t* timerBegin(uint8_t timer, uint16_t divider, bool countUp);
void        timerEnd(hw_timer_t* timer);

void timerAttachInterrupt(hw_timer_t* timer, void (*fn)(void), bool edge);
void timerDetachInterrupt(hw_timer_t* timer);

// Figure this out:
extern "C" {
esp_err_t esp_task_wdt_reset(void);
}

unsigned long micros();
unsigned long millis();
void          delay(uint32_t);
void          delayMicroseconds(uint32_t us);
