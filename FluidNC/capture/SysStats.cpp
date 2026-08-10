#include "Driver/SysStats.h"
#include "NutsBolts.h"

#include <freertos/FreeRTOS.h>
#include <thread>

namespace {
    const char* host_platform_name() {
#ifdef __APPLE__
        return "macOS";
#elif defined(__linux__)
        return "Linux";
#elif defined(_WIN32)
        return "Windows";
#else
        return "Host";
#endif
    }

    unsigned int host_cpu_cores() {
        auto cores = std::thread::hardware_concurrency();
        return cores ? cores : 1;
    }
}

void platform_sys_stats(JSONencoder& j) {
    j.id_value_object("Platform", host_platform_name());
    j.id_value_object("CPU Cores", host_cpu_cores());
    j.id_value_object("Free memory", formatBytes(xPortGetFreeHeapSize()));
}

void platform_sys_stats(Channel& out) {
    log_stream(out, "Platform: " << host_platform_name());
    log_stream(out, "CPU Cores: " << host_cpu_cores());
    log_stream(out, "Free memory: " << formatBytes(xPortGetFreeHeapSize()));
}