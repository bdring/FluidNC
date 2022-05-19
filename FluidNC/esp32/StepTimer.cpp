// Copyright 2022 Mitch Bradley
//
// Interface to the ESP32 alarm timer for step timing

#ifdef __cplusplus
extern "C" {
#endif

// #include "../src/Driver/StepTimer.h"
#include "driver/timer.h"
#include "hal/timer_hal.h"

static const uint32_t fTimers = 80000000;  // the frequency of ESP32 timers

#define USE_LEGACY_TIMER

#ifdef USE_LEGACY_TIMER
#    define TIMER_GROUP_NUM TIMER_GROUP_0, TIMER_0

void stepTimerStart() {
    //    timer_set_counter_value(TIMER_GROUP_NUM, 0);
    //    timer_group_enable_alarm_in_isr(TIMER_GROUP_NUM);
    timer_set_alarm(TIMER_GROUP_NUM, TIMER_ALARM_EN);
    timer_start(TIMER_GROUP_NUM);
}

void IRAM_ATTR stepTimerRestart() {
    // Resetting the counter value here si unnecessary because it
    // happens automatically via the autoreload hardware.
    // The timer framework requires autoreload.
    // If you set autoreload to false, the ISR wrapper will
    // disable the alarm after our ISR function returns, so
    // an attempt to enable the alarm here is ineffective.
    //    timer_set_counter_value(TIMER_GROUP_NUM, 0);
}

uint32_t stepTimerGetTicks() {
    uint64_t val;
    timer_get_alarm_value(TIMER_GROUP_NUM, &val);
    return val;
}

void IRAM_ATTR stepTimerSetTicks(uint32_t ticks) {
    timer_group_set_alarm_value_in_isr(TIMER_GROUP_NUM, (uint64_t)ticks);
}

void IRAM_ATTR stepTimerStop() {
    timer_pause(TIMER_GROUP_NUM);
    //    timer_set_auto_reload(TIMER_GROUP_NUM, TIMER_AUTORELOAD_DIS);
    timer_set_alarm(TIMER_GROUP_NUM, TIMER_ALARM_DIS);
}

void stepTimerInit(uint32_t frequency, bool (*fn)(void)) {
    timer_config_t config = {
        .alarm_en    = TIMER_ALARM_DIS,
        .counter_en  = TIMER_PAUSE,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_EN,
        // .clk_src = TIMER_SRC_CLK_DEFAULT,
        .divider = fTimers / frequency,
    };
    timer_init(TIMER_GROUP_NUM, &config);
    timer_set_counter_value(TIMER_GROUP_NUM, 0);
    timer_isr_callback_add(TIMER_GROUP_NUM, (timer_isr_t)fn, NULL, ESP_INTR_FLAG_IRAM);
    //timer_pause(TIMER_GROUP_NUM);
    // timer_start(TIMER_GROUP_NUM);
}
#else
#    include "driver/gptimer.h"

static gptimer_handle_t gptimer = NULL;
void                    stepTimerInit(uint32_t frequency, bool (*fn)(void)) {
    gptimer_config_t timer_config = {
        .clk_src       = GPTIMER_CLK_SRC_DEFAULT,
        .direction     = GPTIMER_COUNT_UP,
        .resolution_hz = frequency,
    };
    gptimer_new_timer(&timer_config, &gptimer);

    gptimer_event_callbacks_t cbs = {
        .on_alarm = step_timer_cb,
    };
    gptimer_register_event_callbacks(gptimer, &cbs, NULL);

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 1000000,  // period = 1s
    };
    gptimer_set_alarm_action(gptimer, &alarm_config);
    gptimer_pause(gptimer);
}

void IRAM_ATTR stepTimerStart() {
    gptimer_alarm_config_t alarm_config = {
        .reload_count               = 0,
        .alarm_count                = 1,  // Trigger immediately for first pulse
        .flags.auto_reload_on_alarm = false,
    };
    gptimer_set_alarm_action(gptimer, &alarm_config);
}

void IRAM_ATTR stepTimerSetTicks(uint32_t ticks) {
    gptimer_alarm_config_t alarm_config = {
        .reload_count               = 0,
        .alarm_count                = ticks,  // Trigger immediately for first pulse
        .flags.auto_reload_on_alarm = true,
    };
    gptimer_set_alarm_action(gptimer, &alarm_config);
}

void IRAM_ATTR stepTimerStop() {
    gptimer_stop(gptimer);
}
#endif

#ifdef __cplusplus
}
#endif
