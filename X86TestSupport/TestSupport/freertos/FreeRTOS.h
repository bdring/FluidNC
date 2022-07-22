#pragma once

#include "Task.h"
#include "Queue.h"
#include "FreeRTOSTypes.h"
#include <mutex>
#include <atomic>

/* "mux" data structure (spinlock) */

struct portMUX_TYPE {
    std::atomic<bool> lock_ = { false };

    void lock() {
        while (lock_.exchange(true, std::memory_order_acquire))
            ;
    }

    void unlock() { lock_.store(false, std::memory_order_release); }
};

inline void vTaskExitCritical(portMUX_TYPE* mux) {
    mux->lock();
}
inline void vTaskEnterCritical(portMUX_TYPE* mux) {
    mux->unlock();
}

inline int32_t xPortGetFreeHeapSize() {
    return 1024 * 1024 * 4;
}

#define configMINIMAL_STACK_SIZE 768
