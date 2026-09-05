#include "queue.h"

#include <cstring>
#include <atomic>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>

// Ring buffer with an explicit item count, so a queue of length N holds N
// items (matching FreeRTOS) rather than N-1.  readIndex/writeIndex are byte
// offsets into data; count is the source of truth for empty/full.

QueueHandle_t xQueueGenericCreate(const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize, const uint8_t ucQueueType /* =0 */) {
    auto ptr         = new QueueHandle();
    ptr->entrySize   = uxItemSize;
    ptr->numberItems = uxQueueLength;
    ptr->data.resize(uxItemSize * uxQueueLength);
    return ptr;
}

static void advance(QueueHandle_t xQueue, size_t& index) {
    index += xQueue->entrySize;
    if (index == xQueue->data.size()) {
        index = 0;
    }
}

BaseType_t xQueueGenericReceive(QueueHandle_t xQueue, void* const pvBuffer, TickType_t xTicksToWait, const BaseType_t xJustPeek) {
    std::unique_lock<std::mutex> lock(xQueue->mutex);

    if (xQueue->count == 0) {
        if (xTicksToWait == 0) {
            return errQUEUE_EMPTY;  // No wait, return immediately
        }
        if (xTicksToWait == portMAX_DELAY) {
            xQueue->not_empty_cv.wait(lock, [xQueue] { return xQueue->count != 0; });
        } else {
            auto timeout_duration = std::chrono::milliseconds(xTicksToWait * portTICK_PERIOD_MS);
            xQueue->not_empty_cv.wait_for(lock, timeout_duration, [xQueue] { return xQueue->count != 0; });
        }
        if (xQueue->count == 0) {
            return errQUEUE_EMPTY;  // Timeout occurred
        }
    }

    memcpy(pvBuffer, xQueue->data.data() + xQueue->readIndex, xQueue->entrySize);

    if (xJustPeek == pdFALSE) {
        advance(xQueue, xQueue->readIndex);
        --xQueue->count;
        xQueue->not_full_cv.notify_one();  // Notify potential senders
    }
    return pdTRUE;
}

BaseType_t xQueueGenericSendFromISR(QueueHandle_t     xQueue,
                                    const void* const pvItemToQueue,
                                    BaseType_t* const pxHigherPriorityTaskWoken,
                                    const BaseType_t  xCopyPosition) {
    std::lock_guard<std::mutex> lock(xQueue->mutex);

    if (xQueue->count == xQueue->numberItems) {
        return errQUEUE_FULL;
    }

    memcpy(xQueue->data.data() + xQueue->writeIndex, pvItemToQueue, xQueue->entrySize);
    advance(xQueue, xQueue->writeIndex);
    ++xQueue->count;
    xQueue->not_empty_cv.notify_one();  // Notify potential receivers
    return pdTRUE;
}

BaseType_t xQueueGenericReset(QueueHandle_t xQueue, BaseType_t xNewQueue) {
    std::lock_guard<std::mutex> lock(xQueue->mutex);

    xQueue->writeIndex = xQueue->readIndex = 0;
    xQueue->count                          = 0;
    return pdTRUE;
}

BaseType_t xQueueGenericSend(QueueHandle_t xQueue, const void* const pvItemToQueue, TickType_t xTicksToWait, BaseType_t xCopyPosition) {
    std::unique_lock<std::mutex> lock(xQueue->mutex);

    if (xQueue->count == xQueue->numberItems) {
        if (xTicksToWait == 0) {
            return errQUEUE_FULL;  // No wait, return immediately
        }
        if (xTicksToWait == portMAX_DELAY) {
            xQueue->not_full_cv.wait(lock, [xQueue] { return xQueue->count != xQueue->numberItems; });
        } else {
            auto timeout_duration = std::chrono::milliseconds(xTicksToWait * portTICK_PERIOD_MS);
            xQueue->not_full_cv.wait_for(lock, timeout_duration, [xQueue] { return xQueue->count != xQueue->numberItems; });
        }
        if (xQueue->count == xQueue->numberItems) {
            return errQUEUE_FULL;  // Timeout occurred
        }
    }

    memcpy(xQueue->data.data() + xQueue->writeIndex, pvItemToQueue, xQueue->entrySize);
    advance(xQueue, xQueue->writeIndex);
    ++xQueue->count;
    xQueue->not_empty_cv.notify_one();  // Notify potential receivers
    return pdTRUE;
}

UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t xQueue) {
    std::lock_guard<std::mutex> lock(xQueue->mutex);
    return UBaseType_t(xQueue->count);
}

UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t xQueue) {
    std::lock_guard<std::mutex> lock(xQueue->mutex);
    return UBaseType_t(xQueue->numberItems - xQueue->count);
}
