#include "esp32-hal-timer.h"
#include "StdTimer.h"
#include "src/Stepping.h"

uint32_t g_ticks_per_us_pro  = 240 * 1000 * 1000;  // For CPU 0 - typically 240 MHz
uint32_t g_ticks_per_us_app  = 240 * 1000 * 1000;  // For CPU 1 - typically 240 MHz
uint32_t g_internal_timer_us = 1000000000L / Machine::Stepping::fStepperTimer;

StdTimer g_timer(g_internal_timer_us);

hw_timer_s* timerBegin(uint8_t timer, uint16_t divider, bool countUp) {
    return &g_timer;
}

void timerEnd(hw_timer_s* timer) {
    g_timer.stop();
}

void timerAttachInterrupt(hw_timer_s* timer, void (*fn)(void), bool edge) {
    g_timer.set_action(fn);
    g_timer.start();
}

void timerAlarmEnable(hw_timer_t* timer) {
    g_timer.set_enable(true);
}
void timerAlarmDisable(hw_timer_t* timer) {
    g_timer.set_enable(false);
}

void timerWrite(hw_timer_t* timer, uint64_t val) {}

// interruptAt timerTicks/step or ticks between steps
// Machine::fStepperTimer/interruptAt
void timerAlarmWrite(hw_timer_t* timer, uint64_t interruptAt, bool autoreload) {
    g_timer.set_pulse_tic(interruptAt / 100);
}

// Figure this out:
extern "C" {
esp_err_t esp_task_wdt_reset(void) {
    return ESP_OK;
}

void vAssertCalled(unsigned long ulLine, const char* const pcFileName) {}
}
