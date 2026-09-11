#include "task.h"

#include "Capture.h"
#include "Arduino.h"

// This code is based on std::thread, which is actually incorrect. A closer representation would be to
// use thread fibers like MS ConvertThreadToFiber and CreateFiber. That way, we can have 2 threads (one for
// each CPU) and then allocate multiple cooperative (non-preemptive) fibers on it.

#include <thread>
#include <vector>
#include <memory>
#include <mutex>

namespace {
    std::mutex                                threads_mutex;
    std::vector<std::unique_ptr<std::thread>> threads;

    // Identity of the running task, for xTaskGetCurrentTaskHandle(). This can't
    // just be the std::thread's own address: the std::thread ctor launches the
    // task body immediately, before xTaskCreatePinnedToCore can hand that address
    // back via *pvCreatedTask, so a task could race its own creator. Instead each
    // task gets a token allocated before the thread starts, so the task's wrapper
    // can publish it via thread_local before calling into pvTaskCode.
    thread_local TaskHandle_t t_current_task_handle = nullptr;

    std::vector<std::unique_ptr<std::thread>> take_threads() {
        std::lock_guard<std::mutex> lock(threads_mutex);
        std::vector<std::unique_ptr<std::thread>> snapshot;
        snapshot.swap(threads);
        return snapshot;
    }
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t      pvTaskCode,
                                   const char* const   pcName,
                                   const uint32_t      usStackDepth,
                                   void* const         pvParameters,
                                   UBaseType_t         uxPriority,
                                   TaskHandle_t* const pvCreatedTask,
                                   const BaseType_t    xCoreID) {
    (void)pcName;
    (void)usStackDepth;
    (void)uxPriority;
    (void)xCoreID;

    TaskHandle_t handle = new char;  // unique token, never freed: tasks live for the process lifetime

    // Publish the handle to the caller's *pvCreatedTask before the thread starts
    // running (not after, as the naive version would): the std::thread ctor below
    // synchronizes-with the new thread's start, so this ordering is what lets a
    // task that immediately compares against e.g. Protocol.cpp's `pollingTask`
    // see a fully-published value instead of racing the creator for it.
    if (pvCreatedTask != nullptr) {
        *pvCreatedTask = handle;
    }

    std::unique_ptr<std::thread> thread = std::make_unique<std::thread>(
        [pvTaskCode, pvParameters, handle]() {
            t_current_task_handle = handle;
            pvTaskCode(pvParameters);
        });
    {
        std::lock_guard<std::mutex> lock(threads_mutex);
        threads.emplace_back(std::move(thread));
    }
    return pdTRUE;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) {
    return t_current_task_handle;
}

void vTaskDelay(const TickType_t xTicksToDelay) {
    Capture::instance().wait(xTicksToDelay);
}

void cleanup_threads() {
    auto owned_threads = take_threads();
    for (auto& thread : owned_threads) {
        if (thread && thread->joinable()) {
            thread->join();  // Wait for all threads to finish before exit
        }
    }
}

void vTaskDelayUntil(TickType_t* const pxPreviousWakeTime, const TickType_t xTimeIncrement) {
    Capture::instance().waitUntil((*pxPreviousWakeTime + xTimeIncrement));
}

TickType_t xTaskGetTickCount(void) {
    auto& inst = Capture::instance();
    inst.wait(1);
    return inst.current();
}

#ifdef TASK_TIMING
unsigned long millis() {
    return xTaskGetTickCount() / portTICK_PERIOD_MS;
}

unsigned long micros() {
    return 1000 * millis();
}

void delayMicroseconds(uint32_t us) {
    vTaskDelay(us * (portTICK_PERIOD_MS / 1000));  // delay a while
}
#endif

void delay(uint32_t value) {
    vTaskDelay(value * portTICK_PERIOD_MS);  // delay a while
}

void cleanupThreads() {
    cleanup_threads();
}
