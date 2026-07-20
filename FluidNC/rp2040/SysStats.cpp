#include "Driver/SysStats.h"
#include "hardware/flash.h"
#include <RP2040Support.h>
#include <sstream>

void platform_sys_stats(JSONencoder& j) {
    j.id_value_object("Chip ID", rp2040.getChipID());
    j.id_value_object("CPU Cores", 2);

    std::ostringstream msg;
    msg << (rp2040.f_cpu() / 1000000) << "Mhz";
    j.id_value_object("CPU Frequency", msg.str());

    // std::ostringstream msg2;
    // msg2 << std::fixed << std::setprecision(1) << temperatureRead() << "°C";
    // j.id_value_object("CPU Temperature", msg2.str());

    j.id_value_object("Free memory", formatBytes(rp2040.getFreeHeap()));
    //    j.id_value_object("SDK", rp2040.getSdkVersion());
    j.id_value_object("Flash Size", formatBytes(PICO_FLASH_SIZE_BYTES));
}
void platform_sys_stats(Channel& out) {
    log_stream(out, "Chip ID: " << rp2040.getChipID());
    log_stream(out, "CPU Cores: " << 2);
    log_stream(out, "CPU Frequency: " << (rp2040.f_cpu() / 1000000) << "Mhz");

    // std::ostringstream msg;
    // msg << std::fixed << std::setprecision(1) << temperatureRead() << "°C";
    // log_stream(out, "CPU Temperature: " << msg.str());
    log_stream(out, "Free memory: " << formatBytes(rp2040.getFreeHeap()));
    //    log_stream(out, "SDK: " << rp2040.getSdkVersion());
    log_stream(out, "Flash Size: " << formatBytes(PICO_FLASH_SIZE_BYTES));
}
