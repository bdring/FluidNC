#pragma once

#include "../Arduino.h"
#include "FreeRTOS.h"
#include "FreeRTOSTypes.h"

void vTaskDelay(const TickType_t xTicksToDelay);

#define CONFIG_ARDUINO_RUNNING_CORE 0

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t      pvTaskCode,
                                   const char* const   pcName,
                                   const uint32_t      usStackDepth,
                                   void* const         pvParameters,
                                   UBaseType_t         uxPriority,
                                   TaskHandle_t* const pvCreatedTask,
                                   const BaseType_t    xCoreID);

#define tskNO_AFFINITY INT_MAX

static inline IRAM_ATTR BaseType_t xTaskCreate(TaskFunction_t      pvTaskCode,
                                               const char* const   pcName,
                                               const uint32_t      usStackDepth,
                                               void* const         pvParameters,
                                               UBaseType_t         uxPriority,
                                               TaskHandle_t* const pvCreatedTask) {
    return xTaskCreatePinnedToCore(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pvCreatedTask, tskNO_AFFINITY);
}

void vTaskDelayUntil(TickType_t* const pxPreviousWakeTime, const TickType_t xTimeIncrement);

TickType_t xTaskGetTickCount(void);

#define CONFIG_FREERTOS_HZ 1000
#define configTICK_RATE_HZ (CONFIG_FREERTOS_HZ)
#define portTICK_PERIOD_MS ((TickType_t)1000 / configTICK_RATE_HZ)
#define portTICK_RATE_MS portTICK_PERIOD_MS

#define portMUX_FREE_VAL 0xB33FFFFF

/* Special constants for vPortCPUAcquireMutexTimeout() */
#define portMUX_NO_TIMEOUT (-1) /* When passed for 'timeout_cycles', spin forever if necessary */
#define portMUX_TRY_LOCK 0      /* Try to acquire the spinlock a single time only */

// Keep this in sync with the portMUX_TYPE struct definition please.
#ifndef CONFIG_FREERTOS_PORTMUX_DEBUG
#    define portMUX_INITIALIZER_UNLOCKED                                                                                                   \
        {}
#else
#    define portMUX_INITIALIZER_UNLOCKED                                                                                                   \
        { .owner = portMUX_FREE_VAL, .count = 0, .lastLockedFn = "(never locked)", .lastLockedLine = -1 }
#endif
