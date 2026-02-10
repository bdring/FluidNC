#include "queue.h"

#include <cstring>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>

QueueHandle_t xQueueGenericCreate(const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize, const uint8_t ucQueueType /* =0 */) {
    auto ptr         = new QueueHandle();
    ptr->entrySize   = uxItemSize;
    ptr->numberItems = uxQueueLength;
    ptr->data.resize(uxItemSize * uxQueueLength);
    return ptr;
}

BaseType_t xQueueGenericReceive(QueueHandle_t xQueue, void* const pvBuffer, TickType_t xTicksToWait, const BaseType_t xJustPeek) {
    std::unique_lock<std::mutex> lock(xQueue->mutex);

    // Check if data is immediately available
    if (xQueue->readIndex != xQueue->writeIndex) {
        memcpy(pvBuffer, xQueue->data.data() + xQueue->readIndex, xQueue->entrySize);

        if (xJustPeek == pdFALSE) {
            auto newPtr = xQueue->readIndex + xQueue->entrySize;
            if (newPtr == xQueue->data.size()) {
                newPtr = 0;
            }
            xQueue->readIndex = newPtr;
            xQueue->not_full_cv.notify_one(); // Notify potential senders
        }
        return pdTRUE;
    }

    // No data available, handle timeout
    if (xTicksToWait == 0) {
        return errQUEUE_EMPTY; // No wait, return immediately
    }

    // Wait for data with timeout
    bool data_received = false;
    if (xTicksToWait == portMAX_DELAY) {
        // Wait indefinitely
        xQueue->not_empty_cv.wait(lock, [xQueue] {
            return xQueue->readIndex != xQueue->writeIndex;
        });
        data_received = true;
    } else {
        // Wait with timeout
        auto timeout_duration = std::chrono::milliseconds(xTicksToWait * portTICK_PERIOD_MS);
        data_received = xQueue->not_empty_cv.wait_for(lock, timeout_duration, [xQueue] {
            return xQueue->readIndex != xQueue->writeIndex;
        });
    }

    if (data_received && xQueue->readIndex != xQueue->writeIndex) {
        memcpy(pvBuffer, xQueue->data.data() + xQueue->readIndex, xQueue->entrySize);

        if (xJustPeek == pdFALSE) {
            auto newPtr = xQueue->readIndex + xQueue->entrySize;
            if (newPtr == xQueue->data.size()) {
                newPtr = 0;
            }
            xQueue->readIndex = newPtr;
            xQueue->not_full_cv.notify_one(); // Notify potential senders
        }
        return pdTRUE;
    }

    return errQUEUE_EMPTY; // Timeout occurred
}

BaseType_t xQueueGenericSendFromISR(QueueHandle_t     xQueue,
                                    const void* const pvItemToQueue,
                                    BaseType_t* const pxHigherPriorityTaskWoken,
                                    const BaseType_t  xCopyPosition) {
    std::lock_guard<std::mutex> lock(xQueue->mutex);

    auto newPtr = xQueue->writeIndex + xQueue->entrySize;
    if (newPtr == xQueue->data.size()) {
        newPtr = 0;
    }
    if (xQueue->readIndex != newPtr) {
        memcpy(xQueue->data.data() + xQueue->writeIndex, pvItemToQueue, xQueue->entrySize);

        xQueue->writeIndex = newPtr;
        xQueue->not_empty_cv.notify_one(); // Notify potential receivers
        return pdTRUE;
    } else {
        return errQUEUE_FULL;
    }
}

BaseType_t xQueueGenericReset(QueueHandle_t xQueue, BaseType_t xNewQueue) {
    std::lock_guard<std::mutex> lock(xQueue->mutex);

    xQueue->writeIndex = xQueue->readIndex = 0;
    return pdTRUE;
}

BaseType_t xQueueGenericSend(QueueHandle_t xQueue, const void* const pvItemToQueue, TickType_t xTicksToWait, BaseType_t xCopyPosition) {
    std::unique_lock<std::mutex> lock(xQueue->mutex);

    // Calculate next write position
    auto newPtr = xQueue->writeIndex + xQueue->entrySize;
    if (newPtr == xQueue->data.size()) {
        newPtr = 0;
    }

    // Check if space is immediately available
    if (xQueue->readIndex != newPtr) {
        memcpy(xQueue->data.data() + xQueue->writeIndex, pvItemToQueue, xQueue->entrySize);
        xQueue->writeIndex = newPtr;
        xQueue->not_empty_cv.notify_one(); // Notify potential receivers
        return pdTRUE;
    }

    // Queue is full, handle timeout
    if (xTicksToWait == 0) {
        return errQUEUE_FULL; // No wait, return immediately
    }

    // Wait for space with timeout
    bool space_available = false;
    if (xTicksToWait == portMAX_DELAY) {
        // Wait indefinitely
        xQueue->not_full_cv.wait(lock, [xQueue, newPtr] {
            return xQueue->readIndex != newPtr;
        });
        space_available = true;
    } else {
        // Wait with timeout
        auto timeout_duration = std::chrono::milliseconds(xTicksToWait * portTICK_PERIOD_MS);
        space_available = xQueue->not_full_cv.wait_for(lock, timeout_duration, [xQueue, newPtr] {
            return xQueue->readIndex != newPtr;
        });
    }

    // Recalculate newPtr in case it changed during wait
    newPtr = xQueue->writeIndex + xQueue->entrySize;
    if (newPtr == xQueue->data.size()) {
        newPtr = 0;
    }

    if (space_available && xQueue->readIndex != newPtr) {
        memcpy(xQueue->data.data() + xQueue->writeIndex, pvItemToQueue, xQueue->entrySize);
        xQueue->writeIndex = newPtr;
        xQueue->not_empty_cv.notify_one(); // Notify potential receivers
        return pdTRUE;
    }

    return errQUEUE_FULL; // Timeout occurred
}

UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t xQueue) {
    int n = xQueue->writeIndex - xQueue->readIndex;
    if (n < 0) {
        n += xQueue->numberItems;
    }
    return UBaseType_t(n / xQueue->entrySize);
}
