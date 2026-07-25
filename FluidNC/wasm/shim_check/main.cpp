// Second-stage validation for the FluidNC WASM port: proves the *real*
// platform shim FluidNC/src will actually depend on -- capture/arduino
// (Arduino.h/Print/Stream/WString) and capture/freertos (the FreeRTOS
// task/queue/semaphore emulation, built on std::thread/condition_variable)
// -- compiles and runs correctly under Emscripten with -pthread, not just
// bare std::thread the way the first spike (../spike/main.cpp) proved the
// toolchain alone.
//
// Deliberately does NOT pull in capture/gpio.cpp or other capture/*.cpp
// files yet: those reach into real FluidNC/src headers (Pin.h, Uart.h,
// Protocol.h), which is the next, much larger step of actually building
// FluidNC source against this platform layer.

#include <emscripten.h>
#include <cstdio>
#include <cstring>

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

namespace {
QueueHandle_t worker_queue = nullptr;

void worker_task(void* /*param*/) {
    for (int i = 0; i < 5; ++i) {
        int msg = i;
        xQueueSend(worker_queue, &msg, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelay(portMAX_DELAY);  // FreeRTOS tasks don't return; park forever
}
}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
void shim_start_task() {
    if (worker_queue == nullptr) {
        worker_queue = xQueueCreate(10, sizeof(int));
        TaskHandle_t handle = nullptr;
        xTaskCreate(worker_task, "worker", 4096, nullptr, 1, &handle);
    }
}

// Drains whatever the worker task has queued so far (non-blocking) and
// returns how many items were consumed -- proves xQueueSend from one real
// pthread and xQueueReceive from another (the caller's thread) interoperate.
EMSCRIPTEN_KEEPALIVE
int shim_drain_queue() {
    if (worker_queue == nullptr) {
        return -1;
    }
    int count = 0;
    int msg;
    while (xQueueReceive(worker_queue, &msg, 0)) {
        ++count;
    }
    return count;
}

EMSCRIPTEN_KEEPALIVE
unsigned long shim_millis() {
    return millis();
}

// Same shape as the first spike's bridge function, but now the response
// is built with the real Arduino String/Print-adjacent types instead of
// snprintf, proving WString.cpp links correctly.
EMSCRIPTEN_KEEPALIVE
const char* shim_handle_line(const char* line) {
    static String response;
    if (line != nullptr && std::strcmp(line, "?") == 0) {
        response = String("<Idle|WPos:0.000,0.000,0.000|FS:0,0>");
    } else {
        response = String("error: unrecognized line '");
        response += (line ? line : "(null)");
        response += "'";
    }
    return response.c_str();
}

}  // extern "C"
