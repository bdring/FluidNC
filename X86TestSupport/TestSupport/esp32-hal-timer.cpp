#include "esp32-hal-timer.h"

uint32_t g_ticks_per_us_pro = 240 * 1000 * 1000;  // For CPU 0 - typically 240 MHz
uint32_t g_ticks_per_us_app = 240 * 1000 * 1000;  // For CPU 1 - typically 240 MHz

struct hw_timer_s {};

// TODO: These are just stubs.

void timerAlarmEnable(hw_timer_t* timer) {}
void timerAlarmDisable(hw_timer_t* timer) {}
void timerWrite(hw_timer_t* timer, uint64_t val) {}
void timerAlarmWrite(hw_timer_t* timer, uint64_t interruptAt, bool autoreload) {}

hw_timer_t* timerBegin(uint8_t timer, uint16_t divider, bool countUp) {
    return new hw_timer_t();
}

void timerEnd(hw_timer_t* timer) {
    delete timer;
}

void timerAttachInterrupt(hw_timer_t* timer, void (*fn)(void), bool edge) {}
void timerDetachInterrupt(hw_timer_t* timer) {}

// Figure this out:
extern "C" {
esp_err_t esp_task_wdt_reset(void) {
    return ESP_OK;
}
}
