#include "Task.h"

#include "Capture.h"
#include "../Arduino.h"

// This code is based on std::thread, which is actually incorrect. A closer representation would be to
// use thread fibers like MS ConvertThreadToFiber and CreateFiber. That way, we can have 2 threads (one for
// each CPU) and then allocate multiple cooperative (non-preemptive) fibers on it.

#include <thread>
#include <vector>
#include <memory>

std::vector<std::unique_ptr<std::thread>> threads;

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t      pvTaskCode,
                                   const char* const   pcName,
                                   const uint32_t      usStackDepth,
                                   void* const         pvParameters,
                                   UBaseType_t         uxPriority,
                                   TaskHandle_t* const pvCreatedTask,
                                   const BaseType_t    xCoreID) {
    std::unique_ptr<std::thread> thread = std::make_unique<std::thread>(pvTaskCode, pvParameters);
    threads.emplace_back(std::move(thread));
    return pdTRUE;
}

void vTaskDelay(const TickType_t xTicksToDelay) {
    Capture::instance().wait(xTicksToDelay);
    delay(xTicksToDelay);
}

void vTaskDelayUntil(TickType_t* const pxPreviousWakeTime, const TickType_t xTimeIncrement) {
    Capture::instance().waitUntil((*pxPreviousWakeTime + xTimeIncrement));
}

TickType_t xTaskGetTickCount(void) {
    auto& inst = Capture::instance();
    inst.wait(1);
    return inst.current();
}

unsigned long micros() {
    return xTaskGetTickCount() / (portTICK_PERIOD_MS / 1000);
}

unsigned long millis() {
    return xTaskGetTickCount() / portTICK_PERIOD_MS;
}

void delay(uint32_t value) {
    vTaskDelay(value * portTICK_PERIOD_MS);  // delay a while
}

void delayMicroseconds(uint32_t us) {
    vTaskDelay(us * (portTICK_PERIOD_MS / 1000));  // delay a while
}
