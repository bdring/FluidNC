#include "semphr.h"
#include <semaphore>
#include <mutex>
bool xSemaphoreTake(SemaphoreHandle_t lock, TickType_t xTicksToWait) {
    auto sem = lock->semaphore;
    if (sem) {
        if (xTicksToWait == 0) {
            // Equivalent to polling (xTicksToWait = 0)
            return sem->try_acquire();
        } else if (xTicksToWait == portMAX_DELAY) {
            // Equivalent to blocking indefinitely
            sem->acquire();
            return true;
        } else {
            // Equivalent to waiting with a timeout
            return sem->try_acquire_for(std::chrono::milliseconds(xTicksToWait));
        }
    }
    auto mut = lock->mutex;
    if (mut) {
        if (xTicksToWait == 0) {
            // Equivalent to polling (xTicksToWait = 0)
            return mut->try_lock();
        } else if (xTicksToWait == portMAX_DELAY) {
            // Equivalent to blocking indefinitely
            mut->lock();
            return true;
        } else {
            // XXX C++ std::mutex does not have try_lock_for() and FluidNC
            // does not use timed mutexes, so we just treat this as a poll
            return mut->try_lock();
        }
        mut->unlock();
    }
    return false;
}
bool xSemaphoreGive(SemaphoreHandle_t lock) {
    auto sem = lock->semaphore;
    if (sem) {
        sem->release();
        return true;
    }
    auto mut = lock->mutex;
    if (mut) {
        mut->unlock();
        return true;
    }
    return false;
}

SemaphoreHandle_t xSemaphoreCreateBinary() {
    return new Semaphore { nullptr, new std::binary_semaphore(0) };
}
SemaphoreHandle_t xSemaphoreCreateMutex() {
    return new Semaphore { new std::mutex(), nullptr };
}
