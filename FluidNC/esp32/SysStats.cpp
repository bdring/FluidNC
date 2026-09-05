#include "Driver/SysStats.h"
#include "Driver/heap.h"
#include <sstream>
#include <iomanip>

#include <Esp.h>
#include <esp_heap_caps.h>

size_t platform_max_free_block() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

void platform_sys_stats(JSONencoder& j) {
    j.id_value_object("Chip ID", (uint16_t)(ESP.getEfuseMac() >> 32));
    j.id_value_object("CPU Cores", ESP.getChipCores());
    j.id_value_object("CPU Frequency", std::to_string(ESP.getCpuFreqMHz()) + "Mhz");
    j.id_value_object("CPU Temperature", formatFloat(temperatureRead(), 1) + "°C");
    j.id_value_object("Free memory", formatBytes(ESP.getFreeHeap()));
    j.id_value_object("SDK", ESP.getSdkVersion());
    j.id_value_object("Flash Size", formatBytes(ESP.getFlashChipSize()));
}

void platform_sys_stats(Channel& out) {
    log_stream(out, "Chip ID: " << (uint16_t)(ESP.getEfuseMac() >> 32));
    log_stream(out, "CPU Cores: " << ESP.getChipCores());
    log_stream(out, "CPU Frequency: " << ESP.getCpuFreqMHz() << "Mhz");
    log_stream(out, "CPU Temperature: " << formatFloat(temperatureRead(), 1) << "°C");
    log_stream(out, "Free memory: " << formatBytes(ESP.getFreeHeap()));
    log_stream(out, "SDK: " << ESP.getSdkVersion());
    log_stream(out, "Flash Size: " << formatBytes(ESP.getFlashChipSize()));
}
