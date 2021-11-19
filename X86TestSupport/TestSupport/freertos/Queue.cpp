#include "Queue.h"

#include <atomic>
#include <vector>
#include <mutex>

QueueHandle_t xQueueGenericCreate(const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize, const uint8_t ucQueueType /* =0 */) {
    auto ptr         = new QueueHandle();
    ptr->entrySize   = uxItemSize;
    ptr->numberItems = uxQueueLength;
    ptr->data.resize(uxItemSize * uxQueueLength);
    return ptr;
}

BaseType_t xQueueGenericReceive(QueueHandle_t xQueue, void* const pvBuffer, TickType_t xTicksToWait, const BaseType_t xJustPeek) {
    std::lock_guard<std::mutex> lock(xQueue->mutex);

    if (xQueue->readIndex != xQueue->writeIndex) {
        memcpy(pvBuffer, xQueue->data.data() + xQueue->readIndex, xQueue->entrySize);

        auto newPtr = xQueue->readIndex + xQueue->entrySize;
        if (newPtr == xQueue->data.size()) {
            newPtr = 0;
        }
        xQueue->readIndex = newPtr;

        return pdTRUE;
    } else {
        return errQUEUE_FULL;  // no receive
    }
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
    return xQueueGenericSendFromISR(xQueue, pvItemToQueue, nullptr, xCopyPosition);
}
