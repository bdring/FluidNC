#include "semphr.h"
#include <semaphore>
#include <mutex>

bool xSemaphoreTake(SemaphoreHandle_t lock, TickType_t xTicksToWait) {
    // Handle counting semaphore
    if (lock->count != nullptr) {
        if (lock->count_lock) {
            bool acquired = false;
            if (xTicksToWait == 0) {
                acquired = lock->count_lock->try_lock();
            } else if (xTicksToWait == portMAX_DELAY) {
                lock->count_lock->lock();
                acquired = true;
            } else {
                acquired = lock->count_lock->try_lock();
            }
            
            if (acquired) {
                if (*lock->count > 0) {
                    (*lock->count)--;
                    lock->count_lock->unlock();
                    return true;
                } else {
                    lock->count_lock->unlock();
                    return false;  // No permits available
                }
            }
            return false;
        }
        return false;
    }
    
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
    // Handle counting semaphore
    if (lock->count != nullptr) {
        if (lock->count_lock) {
            lock->count_lock->lock();
            (*lock->count)++;
            lock->count_lock->unlock();
            return true;
        }
        return false;
    }
    
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
    return new Semaphore { nullptr, new std::binary_semaphore(0), nullptr, nullptr };
}

SemaphoreHandle_t xSemaphoreCreateMutex() {
    return new Semaphore { new std::mutex(), nullptr, nullptr, nullptr };
}

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount) {
    return new Semaphore { nullptr, nullptr, new int(uxInitialCount), new std::mutex() };
}

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore) {
    if (xSemaphore == nullptr) {
        return;
    }
    
    if (xSemaphore->mutex != nullptr) {
        delete xSemaphore->mutex;
        xSemaphore->mutex = nullptr;
    }
    
    if (xSemaphore->semaphore != nullptr) {
        delete xSemaphore->semaphore;
        xSemaphore->semaphore = nullptr;
    }
    
    if (xSemaphore->count != nullptr) {
        delete xSemaphore->count;
        xSemaphore->count = nullptr;
    }
    
    if (xSemaphore->count_lock != nullptr) {
        delete xSemaphore->count_lock;
        xSemaphore->count_lock = nullptr;
    }
    
    delete xSemaphore;
}
