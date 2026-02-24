#pragma once
#include <semaphore>
#include <mutex>
#include "FreeRTOSTypes.h"

struct Semaphore {
    std::mutex*            mutex     = nullptr;
    std::binary_semaphore* semaphore = nullptr;
};
typedef Semaphore* SemaphoreHandle_t;

bool xSemaphoreTake(SemaphoreHandle_t semv, TickType_t xTicksToWait);
bool xSemaphoreGive(SemaphoreHandle_t semv);

SemaphoreHandle_t xSemaphoreCreateBinary();
SemaphoreHandle_t xSemaphoreCreateMutex();
