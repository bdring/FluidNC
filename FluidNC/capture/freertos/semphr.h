#pragma once
#include <semaphore>
#include <mutex>
#include "FreeRTOSTypes.h"

struct Semaphore {
    std::mutex*            mutex      = nullptr;
    std::binary_semaphore* semaphore  = nullptr;
    int*                   count      = nullptr;  // For counting semaphores
    std::mutex*            count_lock = nullptr;  // Lock for count access
};
typedef Semaphore* SemaphoreHandle_t;

bool xSemaphoreTake(SemaphoreHandle_t semv, TickType_t xTicksToWait);
bool xSemaphoreGive(SemaphoreHandle_t semv);

SemaphoreHandle_t xSemaphoreCreateBinary();
SemaphoreHandle_t xSemaphoreCreateMutex();
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount);
void vSemaphoreDelete(SemaphoreHandle_t xSemaphore);
