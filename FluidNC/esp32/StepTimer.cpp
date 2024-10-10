// Copyright 2022 Mitch Bradley
//
// Interface to the ESP32 alarm timer for step timing
// Uses the timer_ll API from ESP-IDF v4.4.1

#ifdef __cplusplus
extern "C" {
#endif

#include "hal/timer_ll.h"
#include "esp_intr_alloc.h"

static const uint32_t fTimers = 80000000;  // the frequency of ESP32 timers

static bool (*timer_isr_callback)(void);

static void IRAM_ATTR timer_isr(void* arg) {
    // esp_intr_alloc_intrstatus() takes care of filtering based on the interrupt status register
    timer_ll_clear_intr_status(&TIMERG0, TIMER_0);

    if (timer_isr_callback()) {
        // We could just pass the result of timer_isr_callback() as
        // the argument to timer_ll_set_alarm_enable(), but the
        // enable is automatically cleared when the alarm occurs,
        // so setting it to false is redundant.  Writing the
        // device register is much slower than a branch, so
        // this way of doing it is the most efficient.
        timer_ll_set_alarm_enable(&TIMERG0, TIMER_0, true);
    }
}

// Possibly-unnecessary optimization to avoid rewriting the alarm value
static uint32_t old_ticks = 0xffffffff;

void IRAM_ATTR stepTimerStart() {
    timer_ll_set_alarm_value(&TIMERG0, TIMER_0, 10ULL);  // Interrupt very soon to start the stepping
    old_ticks = 10ULL;
    timer_ll_set_alarm_enable(&TIMERG0, TIMER_0, true);
    timer_ll_set_counter_enable(&TIMERG0, TIMER_0, true);
}

void IRAM_ATTR stepTimerSetTicks(uint32_t ticks) {
    if (ticks != old_ticks) {
        timer_ll_set_alarm_value(&TIMERG0, TIMER_0, (uint64_t)ticks);
        old_ticks = ticks;
    }
}

void IRAM_ATTR stepTimerStop() {
    timer_ll_set_counter_enable(&TIMERG0, TIMER_0, false);
    timer_ll_set_alarm_enable(&TIMERG0, TIMER_0, false);
}

void stepTimerInit(uint32_t frequency, bool (*callback)(void)) {
    timer_ll_intr_disable(&TIMERG0, TIMER_0);
    timer_ll_set_counter_enable(&TIMERG0, TIMER_0, TIMER_PAUSE);
    timer_ll_set_counter_value(&TIMERG0, TIMER_0, 0ULL);

    timer_ll_set_divider(&TIMERG0, TIMER_0, fTimers / frequency);
    timer_ll_set_counter_increase(&TIMERG0, TIMER_0, true);
    timer_ll_intr_disable(&TIMERG0, TIMER_0);
    timer_ll_clear_intr_status(&TIMERG0, TIMER_0);
    timer_ll_set_alarm_enable(&TIMERG0, TIMER_0, false);
    timer_ll_set_auto_reload(&TIMERG0, TIMER_0, true);
    timer_ll_set_counter_enable(&TIMERG0, TIMER_0, false);
    timer_ll_set_counter_value(&TIMERG0, TIMER_0, 0);

    timer_isr_callback = callback;

    esp_intr_alloc_intrstatus(timer_group_periph_signals.groups[TIMER_GROUP_0].t0_irq_id,
                              ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3,
                              timer_ll_get_intr_status_reg(&TIMERG0),
                              1 << TIMER_0,
                              timer_isr,
                              NULL,
                              NULL);

    timer_ll_intr_enable(&TIMERG0, TIMER_0);
}

#ifdef __cplusplus
}
#endif
