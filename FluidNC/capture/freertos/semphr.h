#pragma once

#include "FreeRTOS.h"
#include "FreeRTOSTypes.h"

#include <condition_variable>
#include <mutex>

struct SemaphoreHandle {
    std::mutex mutex;
    std::condition_variable cv;
    bool available = false;
};

using SemaphoreHandle_t = SemaphoreHandle*;

inline SemaphoreHandle_t xSemaphoreCreateBinary() {
    return new SemaphoreHandle();
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
    if (semaphore == nullptr) {
        return pdFALSE;
    }
    {
        std::lock_guard<std::mutex> lock(semaphore->mutex);
        semaphore->available = true;
    }
    semaphore->cv.notify_one();
    return pdTRUE;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks_to_wait) {
    if (semaphore == nullptr) {
        return pdFALSE;
    }

    std::unique_lock<std::mutex> lock(semaphore->mutex);
    if (ticks_to_wait == 0) {
        if (!semaphore->available) {
            return pdFALSE;
        }
    } else {
        semaphore->cv.wait(lock, [&]() { return semaphore->available; });
    }

    semaphore->available = false;
    return pdTRUE;
}
