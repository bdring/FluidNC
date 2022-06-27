// Copyright 2022 Mitch Bradley
//
// Interface to the ESP32 alarm timer for step timing
// Uses the timer_hal API from ESP-IDF v4.4.1

#ifdef __cplusplus
extern "C" {
#endif

#include "hal/timer_hal.h"
#include "esp_intr_alloc.h"

static const uint32_t fTimers = 80000000;  // the frequency of ESP32 timers

static timer_hal_context_t hal;

static bool (*timer_isr_callback)(void);

static void IRAM_ATTR timer_isr(void* arg) {
    // esp_intr_alloc_intrstatus() takes care of filtering based on the interrupt status register
    timer_hal_clear_intr_status(&hal);
    timer_isr_callback();
}

void IRAM_ATTR stepTimerStart() {
    timer_hal_set_alarm_enable(&hal, true);
    timer_hal_set_counter_enable(&hal, true);
}

void IRAM_ATTR stepTimerRestart() {
    // Resetting the counter value here is unnecessary because it
    // happens automatically via the autoreload hardware.
    // Newer versions of the timer_ll API do not implement
    // _set_counter_value(), perhaps because of clock domain
    // hazards.
    timer_hal_set_alarm_enable(&hal, true);
}

void IRAM_ATTR stepTimerSetTicks(uint32_t ticks) {
    timer_hal_set_alarm_value(&hal, (uint64_t)ticks);
}

void IRAM_ATTR stepTimerStop() {
    timer_hal_set_counter_enable(&hal, false);
    timer_hal_set_alarm_enable(&hal, false);
}

void stepTimerInit(uint32_t frequency, bool (*callback)(void)) {
    timer_hal_init(&hal, TIMER_GROUP_0, TIMER_0);
    timer_hal_set_counter_value(&hal, 0);

    timer_hal_set_divider(&hal, fTimers / frequency);
    timer_hal_set_counter_increase(&hal, true);
    timer_hal_intr_disable(&hal);
    timer_hal_clear_intr_status(&hal);
    timer_hal_set_alarm_enable(&hal, false);
    timer_hal_set_auto_reload(&hal, true);
    timer_hal_set_counter_enable(&hal, false);
    timer_hal_set_counter_value(&hal, 0);

    timer_isr_callback = callback;

    esp_intr_alloc_intrstatus(timer_group_periph_signals.groups[TIMER_GROUP_0].t0_irq_id,
                              ESP_INTR_FLAG_IRAM,
                              (uint32_t)timer_hal_get_intr_status_reg(&hal),
                              1 << TIMER_0,
                              timer_isr,
                              NULL,
                              NULL);

    timer_hal_intr_enable(&hal);
}

#ifdef __cplusplus
}
#endif
